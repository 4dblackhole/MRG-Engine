#include "Text/TextRendering.h"

#include "Shader/ShaderCompiler.h"

#include <d3dcompiler.h>
#include <dwrite_3.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#pragma comment(lib, "dwrite.lib")

namespace
{
    using Microsoft::WRL::ComPtr;

    constexpr std::uint32_t AtlasWidth = 1024;
    constexpr std::uint32_t AtlasHeight = 1024;
    constexpr std::uint32_t MaximumAtlasPages = 16;
    constexpr std::uint32_t GlyphPadding = 1;

    void ThrowIfFailed(const HRESULT result, const char* operation)
    {
        if (FAILED(result))
        {
            throw std::runtime_error(
                std::string(operation) +
                " failed with HRESULT " +
                std::to_string(static_cast<unsigned long>(result)) + ".");
        }
    }

    [[nodiscard]] D3D12_HEAP_PROPERTIES HeapProperties(
        const D3D12_HEAP_TYPE type) noexcept
    {
        D3D12_HEAP_PROPERTIES properties{};
        properties.Type = type;
        properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        properties.CreationNodeMask = 1;
        properties.VisibleNodeMask = 1;
        return properties;
    }

    [[nodiscard]] D3D12_RESOURCE_DESC BufferDescription(
        const std::size_t sizeBytes) noexcept
    {
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        description.Alignment = 0;
        description.Width = static_cast<UINT64>(sizeBytes);
        description.Height = 1;
        description.DepthOrArraySize = 1;
        description.MipLevels = 1;
        description.Format = DXGI_FORMAT_UNKNOWN;
        description.SampleDesc.Count = 1;
        description.SampleDesc.Quality = 0;
        description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        description.Flags = D3D12_RESOURCE_FLAG_NONE;
        return description;
    }

    [[nodiscard]] D3D12_RESOURCE_BARRIER TransitionBarrier(
        ID3D12Resource& resource,
        const D3D12_RESOURCE_STATES before,
        const D3D12_RESOURCE_STATES after) noexcept
    {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = &resource;
        barrier.Transition.Subresource =
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        return barrier;
    }

    [[nodiscard]] std::uint32_t Align(
        const std::uint32_t value,
        const std::uint32_t alignment) noexcept
    {
        return (value + alignment - 1U) & ~(alignment - 1U);
    }

    [[nodiscard]] std::wstring LocalizedString(
        IDWriteLocalizedStrings& strings)
    {
        UINT32 index = 0;
        BOOL exists = FALSE;
        if (FAILED(strings.FindLocaleName(L"en-us", &index, &exists)) ||
            !exists)
        {
            index = 0;
        }

        UINT32 length = 0;
        ThrowIfFailed(
            strings.GetStringLength(index, &length),
            "IDWriteLocalizedStrings::GetStringLength");
        std::wstring value(length + 1U, L'\0');
        ThrowIfFailed(
            strings.GetString(index, value.data(), length + 1U),
            "IDWriteLocalizedStrings::GetString");
        value.resize(length);
        return value;
    }

    [[nodiscard]] DWRITE_TEXT_ALIGNMENT ToDirectWrite(
        const mrg::graphics::TextHorizontalAlignment alignment) noexcept
    {
        switch (alignment)
        {
        case mrg::graphics::TextHorizontalAlignment::Center:
            return DWRITE_TEXT_ALIGNMENT_CENTER;
        case mrg::graphics::TextHorizontalAlignment::Trailing:
            return DWRITE_TEXT_ALIGNMENT_TRAILING;
        case mrg::graphics::TextHorizontalAlignment::Leading:
        default:
            return DWRITE_TEXT_ALIGNMENT_LEADING;
        }
    }

    [[nodiscard]] DWRITE_PARAGRAPH_ALIGNMENT ToDirectWrite(
        const mrg::graphics::TextVerticalAlignment alignment) noexcept
    {
        switch (alignment)
        {
        case mrg::graphics::TextVerticalAlignment::Center:
            return DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
        case mrg::graphics::TextVerticalAlignment::Far:
            return DWRITE_PARAGRAPH_ALIGNMENT_FAR;
        case mrg::graphics::TextVerticalAlignment::Near:
        default:
            return DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
        }
    }

    template <typename Value>
    void HashCombine(std::size_t& seed, const Value& value) noexcept
    {
        const std::size_t hash = std::hash<Value>{}(value);
        seed ^= hash + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
    }
}

namespace mrg::graphics
{
    struct Font::Impl final
    {
        std::wstring familyName;
        ComPtr<IDWriteFontCollection> collection;
    };

    Font::Font(std::shared_ptr<Impl> implementation)
        : implementation_(std::move(implementation))
    {
    }

    Font::~Font() = default;

    std::wstring_view Font::FamilyName() const noexcept
    {
        return implementation_->familyName;
    }

    struct TextRenderSystem::Impl final
    {
        struct GlyphInstance final
        {
            DirectX::XMFLOAT4X4 transform{};
            DirectX::XMFLOAT2 positionPixels{};
            DirectX::XMFLOAT2 sizePixels{};
            DirectX::XMFLOAT4 uvRectangle{};
            DirectX::XMFLOAT4 color{1.0F, 1.0F, 1.0F, 1.0F};
            float depth{};
            std::uint32_t pageIndex{};
        };

        struct GpuGlyphInstance final
        {
            DirectX::XMFLOAT4X4 transform{};
            DirectX::XMFLOAT2 positionPixels{};
            DirectX::XMFLOAT2 sizePixels{};
            DirectX::XMFLOAT4 uvRectangle{};
            DirectX::XMFLOAT4 color{1.0F, 1.0F, 1.0F, 1.0F};
            float depth{};
        };

        static_assert(sizeof(GpuGlyphInstance) == 116);

        struct FrameInstanceBuffer final
        {
            ComPtr<ID3D12Resource> resource;
            std::byte* mappedData{};
            std::size_t capacity{};
            std::vector<ComPtr<ID3D12Resource>> uploadResources;
        };

        struct AtlasPage final
        {
            ComPtr<ID3D12Resource> texture;
            std::uint32_t nextX{GlyphPadding};
            std::uint32_t nextY{GlyphPadding};
            std::uint32_t rowHeight{};
        };

        struct PendingGlyphUpload final
        {
            std::uint32_t pageIndex{};
            std::uint32_t destinationX{};
            std::uint32_t destinationY{};
            std::uint32_t width{};
            std::uint32_t height{};
            std::vector<std::uint8_t> pixels;
        };

        struct GlyphKey final
        {
            std::uintptr_t fontFaceIdentity{};
            std::uint32_t fontSizeBits{};
            std::uint16_t glyphIndex{};
            DWRITE_MEASURING_MODE measuringMode{};

            [[nodiscard]] bool operator==(const GlyphKey&) const noexcept =
                default;
        };

        struct GlyphKeyHash final
        {
            [[nodiscard]] std::size_t operator()(
                const GlyphKey& key) const noexcept
            {
                std::size_t result = 0;
                HashCombine(result, key.fontFaceIdentity);
                HashCombine(result, key.fontSizeBits);
                HashCombine(result, key.glyphIndex);
                HashCombine(
                    result,
                    static_cast<std::uint32_t>(key.measuringMode));
                return result;
            }
        };

        struct GlyphEntry final
        {
            ComPtr<IUnknown> fontFaceIdentity;
            std::uint32_t pageIndex{};
            DirectX::XMFLOAT2 sizePixels{};
            DirectX::XMFLOAT2 bearingPixels{};
            DirectX::XMFLOAT4 uvRectangle{};
            bool drawable{};
        };

        struct LayoutKey final
        {
            FontHandle font;
            std::wstring text;
            std::uint32_t fontSizeBits{};
            std::uint32_t widthBits{};
            std::uint32_t heightBits{};
            TextHorizontalAlignment horizontalAlignment{};
            TextVerticalAlignment verticalAlignment{};

            [[nodiscard]] bool operator==(
                const LayoutKey& other) const noexcept
            {
                return font.get() == other.font.get() &&
                    text == other.text &&
                    fontSizeBits == other.fontSizeBits &&
                    widthBits == other.widthBits &&
                    heightBits == other.heightBits &&
                    horizontalAlignment == other.horizontalAlignment &&
                    verticalAlignment == other.verticalAlignment;
            }
        };

        struct LayoutKeyHash final
        {
            [[nodiscard]] std::size_t operator()(
                const LayoutKey& key) const noexcept
            {
                std::size_t result = 0;
                HashCombine(
                    result,
                    reinterpret_cast<std::uintptr_t>(key.font.get()));
                HashCombine(result, key.text);
                HashCombine(result, key.fontSizeBits);
                HashCombine(result, key.widthBits);
                HashCombine(result, key.heightBits);
                HashCombine(
                    result,
                    static_cast<std::uint32_t>(
                        key.horizontalAlignment));
                HashCombine(
                    result,
                    static_cast<std::uint32_t>(
                        key.verticalAlignment));
                return result;
            }
        };

        class GlyphRunCollector final : public IDWriteTextRenderer
        {
        public:
            GlyphRunCollector(
                Impl& owner,
                const DirectX::XMFLOAT4& color,
                const float depth,
                const DirectX::XMFLOAT4X4& transform) noexcept
                : owner_(owner),
                  color_(color),
                  depth_(depth),
                  transform_(transform)
            {
            }

            HRESULT STDMETHODCALLTYPE QueryInterface(
                REFIID interfaceId,
                void** object) override
            {
                if (object == nullptr)
                {
                    return E_POINTER;
                }
                *object = nullptr;
                if (interfaceId == __uuidof(IUnknown) ||
                    interfaceId == __uuidof(IDWritePixelSnapping) ||
                    interfaceId == __uuidof(IDWriteTextRenderer))
                {
                    *object = static_cast<IDWriteTextRenderer*>(this);
                    AddRef();
                    return S_OK;
                }
                return E_NOINTERFACE;
            }

            ULONG STDMETHODCALLTYPE AddRef() override
            {
                return ++referenceCount_;
            }

            ULONG STDMETHODCALLTYPE Release() override
            {
                const ULONG remaining = --referenceCount_;
                if (remaining == 0)
                {
                    delete this;
                }
                return remaining;
            }

            HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(
                void*,
                BOOL* disabled) override
            {
                if (disabled == nullptr)
                {
                    return E_POINTER;
                }
                *disabled = FALSE;
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE GetCurrentTransform(
                void*,
                DWRITE_MATRIX* transform) override
            {
                if (transform == nullptr)
                {
                    return E_POINTER;
                }
                *transform = DWRITE_MATRIX{
                    1.0F,
                    0.0F,
                    0.0F,
                    1.0F,
                    0.0F,
                    0.0F};
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE GetPixelsPerDip(
                void*,
                FLOAT* pixelsPerDip) override
            {
                if (pixelsPerDip == nullptr)
                {
                    return E_POINTER;
                }
                *pixelsPerDip = 1.0F;
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE DrawGlyphRun(
                void*,
                const FLOAT baselineOriginX,
                const FLOAT baselineOriginY,
                const DWRITE_MEASURING_MODE measuringMode,
                const DWRITE_GLYPH_RUN* glyphRun,
                const DWRITE_GLYPH_RUN_DESCRIPTION*,
                IUnknown*) override
            {
                if (glyphRun == nullptr)
                {
                    return E_INVALIDARG;
                }

                try
                {
                    owner_.AppendGlyphRun(
                        baselineOriginX,
                        baselineOriginY,
                        measuringMode,
                        *glyphRun,
                        color_,
                        depth_,
                        transform_);
                    return S_OK;
                }
                catch (...)
                {
                    error_ = std::current_exception();
                    return E_FAIL;
                }
            }

            HRESULT STDMETHODCALLTYPE DrawUnderline(
                void*,
                FLOAT,
                FLOAT,
                const DWRITE_UNDERLINE*,
                IUnknown*) override
            {
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE DrawStrikethrough(
                void*,
                FLOAT,
                FLOAT,
                const DWRITE_STRIKETHROUGH*,
                IUnknown*) override
            {
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE DrawInlineObject(
                void*,
                FLOAT,
                FLOAT,
                IDWriteInlineObject*,
                BOOL,
                BOOL,
                IUnknown*) override
            {
                return E_NOTIMPL;
            }

            [[nodiscard]] const std::exception_ptr& Error() const noexcept
            {
                return error_;
            }

        private:
            std::atomic<ULONG> referenceCount_{1};
            Impl& owner_;
            DirectX::XMFLOAT4 color_{};
            float depth_{};
            DirectX::XMFLOAT4X4 transform_{};
            std::exception_ptr error_;
        };

        void Initialize(
            ID3D12Device& newDevice,
            const DXGI_FORMAT renderTargetFormat,
            const DXGI_FORMAT newDepthStencilFormat)
        {
            if (initialized)
            {
                throw std::logic_error(
                    "TextRenderSystem is already initialized.");
            }

            device = &newDevice;
            depthStencilFormat = newDepthStencilFormat;
            ThrowIfFailed(
                DWriteCreateFactory(
                    DWRITE_FACTORY_TYPE_SHARED,
                    __uuidof(IDWriteFactory5),
                    reinterpret_cast<IUnknown**>(
                        directWriteFactory.ReleaseAndGetAddressOf())),
                "DWriteCreateFactory");

            CreateDescriptorHeap();
            CreatePipeline(renderTargetFormat);
            initialized = true;
        }

        void Shutdown() noexcept
        {
            for (FrameInstanceBuffer& frame : frameBuffers)
            {
                if (frame.resource != nullptr &&
                    frame.mappedData != nullptr)
                {
                    frame.resource->Unmap(0, nullptr);
                }
                frame = {};
            }

            pendingInstances.clear();
            pendingUploads.clear();
            glyphCache.clear();
            layoutCache.clear();
            atlasPages.clear();
            pipelineState.Reset();
            rootSignature.Reset();
            atlasDescriptorHeap.Reset();
            directWriteFactory.Reset();
            device = nullptr;
            depthStencilFormat = DXGI_FORMAT_UNKNOWN;
            currentFrameIndex = 0;
            initialized = false;
            frameOpen = false;
        }

        [[nodiscard]] FontHandle LoadSystemFont(
            const std::wstring_view familyName)
        {
            RequireInitialized();
            if (familyName.empty())
            {
                throw std::invalid_argument(
                    "A system font family name cannot be empty.");
            }

            const std::wstring family(familyName);
            ComPtr<IDWriteFontCollection> collection;
            ThrowIfFailed(
                directWriteFactory->GetSystemFontCollection(
                    collection.ReleaseAndGetAddressOf()),
                "IDWriteFactory::GetSystemFontCollection");
            UINT32 familyIndex = 0;
            BOOL exists = FALSE;
            ThrowIfFailed(
                collection->FindFamilyName(
                    family.c_str(),
                    &familyIndex,
                    &exists),
                "IDWriteFontCollection::FindFamilyName");
            if (!exists)
            {
                throw std::runtime_error(
                    "The requested system font family is unavailable.");
            }

            auto fontImplementation = std::make_shared<Font::Impl>();
            fontImplementation->familyName = family;
            return FontHandle(
                new Font(std::move(fontImplementation)));
        }

        [[nodiscard]] FontHandle LoadFontFile(
            const std::filesystem::path& fontFile)
        {
            RequireInitialized();
            const std::filesystem::path absolutePath =
                std::filesystem::absolute(fontFile);
            if (!std::filesystem::is_regular_file(absolutePath))
            {
                throw std::runtime_error(
                    "Font file was not found: " +
                    absolutePath.string());
            }

            ComPtr<IDWriteFontFile> directWriteFontFile;
            ThrowIfFailed(
                directWriteFactory->CreateFontFileReference(
                    absolutePath.c_str(),
                    nullptr,
                    directWriteFontFile.ReleaseAndGetAddressOf()),
                "IDWriteFactory::CreateFontFileReference");

            ComPtr<IDWriteFontSetBuilder1> fontSetBuilder;
            ThrowIfFailed(
                directWriteFactory->CreateFontSetBuilder(
                    fontSetBuilder.ReleaseAndGetAddressOf()),
                "IDWriteFactory5::CreateFontSetBuilder");
            ThrowIfFailed(
                fontSetBuilder->AddFontFile(directWriteFontFile.Get()),
                "IDWriteFontSetBuilder1::AddFontFile");

            ComPtr<IDWriteFontSet> fontSet;
            ThrowIfFailed(
                fontSetBuilder->CreateFontSet(
                    fontSet.ReleaseAndGetAddressOf()),
                "IDWriteFontSetBuilder::CreateFontSet");

            ComPtr<IDWriteFontCollection1> fontCollection;
            ThrowIfFailed(
                directWriteFactory->CreateFontCollectionFromFontSet(
                    fontSet.Get(),
                    fontCollection.ReleaseAndGetAddressOf()),
                "IDWriteFactory3::CreateFontCollectionFromFontSet");
            if (fontCollection->GetFontFamilyCount() == 0)
            {
                throw std::runtime_error(
                    "The font file contains no font families.");
            }

            ComPtr<IDWriteFontFamily> family;
            ThrowIfFailed(
                fontCollection->GetFontFamily(
                    0,
                    family.ReleaseAndGetAddressOf()),
                "IDWriteFontCollection::GetFontFamily");
            ComPtr<IDWriteLocalizedStrings> familyNames;
            ThrowIfFailed(
                family->GetFamilyNames(
                    familyNames.ReleaseAndGetAddressOf()),
                "IDWriteFontFamily::GetFamilyNames");

            auto fontImplementation = std::make_shared<Font::Impl>();
            fontImplementation->familyName =
                LocalizedString(*familyNames.Get());
            ThrowIfFailed(
                fontCollection.As(&fontImplementation->collection),
                "IDWriteFontCollection1::QueryInterface");
            return FontHandle(
                new Font(std::move(fontImplementation)));
        }

        void BeginFrame(const std::uint32_t frameIndex)
        {
            RequireInitialized();
            if (frameIndex >= FrameCount)
            {
                throw std::out_of_range(
                    "The text frame index is outside the frame ring.");
            }
            if (frameOpen)
            {
                throw std::logic_error(
                    "A text rendering frame is already open.");
            }

            currentFrameIndex = frameIndex;
            pendingInstances.clear();
            pendingUploads.clear();
            frameBuffers[frameIndex].uploadResources.clear();
            frameOpen = true;
        }

        void Submit(
            const std::wstring_view text,
            const TextDrawCommand& command)
        {
            RequireFrameOpen();
            if (text.empty())
            {
                return;
            }
            if (command.style.font == nullptr)
            {
                throw std::invalid_argument(
                    "TextStyle requires a valid FontHandle.");
            }
            if (!std::isfinite(command.style.fontSizePixels) ||
                command.style.fontSizePixels <= 0.0F)
            {
                throw std::invalid_argument(
                    "Text font size must be positive and finite.");
            }
            if (!std::isfinite(command.layoutSizePixels.x) ||
                !std::isfinite(command.layoutSizePixels.y) ||
                command.layoutSizePixels.x <= 0.0F ||
                command.layoutSizePixels.y <= 0.0F)
            {
                throw std::invalid_argument(
                    "Text layout size must be positive and finite.");
            }

            ComPtr<IDWriteTextLayout> layout =
                FindOrCreateLayout(text, command);
            ComPtr<GlyphRunCollector> collector;
            collector.Attach(
                new GlyphRunCollector(
                    *this,
                    command.style.color,
                    command.depth,
                    command.transform));
            const HRESULT drawResult = layout->Draw(
                nullptr,
                collector.Get(),
                command.positionPixels.x,
                command.positionPixels.y);
            if (collector->Error())
            {
                std::rethrow_exception(collector->Error());
            }
            ThrowIfFailed(drawResult, "IDWriteTextLayout::Draw");
        }

        void Flush(
            ID3D12GraphicsCommandList& commandList,
            const std::uint32_t viewportWidth,
            const std::uint32_t viewportHeight)
        {
            RequireFrameOpen();
            if (viewportWidth == 0 || viewportHeight == 0)
            {
                pendingInstances.clear();
                pendingUploads.clear();
                frameOpen = false;
                return;
            }

            // New glyph bitmaps must reach their atlas pages before any
            // instance in this frame samples them.
            UploadNewGlyphs(commandList);
            if (!pendingInstances.empty())
            {
                // One atlas page is bound per draw. Sorting makes all glyphs
                // for a page one contiguous instance-buffer range.
                std::stable_sort(
                    pendingInstances.begin(),
                    pendingInstances.end(),
                    [](const GlyphInstance& first, const GlyphInstance& second)
                    {
                        return first.pageIndex < second.pageIndex;
                    });
                EnsureInstanceCapacity(pendingInstances.size());

                FrameInstanceBuffer& frame =
                    frameBuffers[currentFrameIndex];
                auto* destination =
                    reinterpret_cast<GpuGlyphInstance*>(frame.mappedData);
                for (std::size_t index = 0;
                     index < pendingInstances.size();
                     ++index)
                {
                    const GlyphInstance& source = pendingInstances[index];
                    destination[index] = GpuGlyphInstance{
                        source.transform,
                        source.positionPixels,
                        source.sizePixels,
                        source.uvRectangle,
                        source.color,
                        source.depth};
                }

                ID3D12DescriptorHeap* heaps[]{
                    atlasDescriptorHeap.Get()};
                commandList.SetDescriptorHeaps(1, heaps);
                commandList.SetGraphicsRootSignature(rootSignature.Get());
                commandList.SetPipelineState(pipelineState.Get());
                commandList.IASetPrimitiveTopology(
                    D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

                const std::array viewportConstants{
                    std::bit_cast<std::uint32_t>(
                        static_cast<float>(viewportWidth)),
                    std::bit_cast<std::uint32_t>(
                        static_cast<float>(viewportHeight))};
                commandList.SetGraphicsRoot32BitConstants(
                    2,
                    static_cast<UINT>(viewportConstants.size()),
                    viewportConstants.data(),
                    0);

                // Emit one instanced draw for each consecutive atlas page.
                const D3D12_GPU_DESCRIPTOR_HANDLE heapStart =
                    atlasDescriptorHeap->
                        GetGPUDescriptorHandleForHeapStart();
                std::size_t firstInstance = 0;
                while (firstInstance < pendingInstances.size())
                {
                    const std::uint32_t pageIndex =
                        pendingInstances[firstInstance].pageIndex;
                    std::size_t endInstance = firstInstance + 1;
                    while (endInstance < pendingInstances.size() &&
                        pendingInstances[endInstance].pageIndex == pageIndex)
                    {
                        ++endInstance;
                    }

                    D3D12_GPU_DESCRIPTOR_HANDLE pageDescriptor = heapStart;
                    pageDescriptor.ptr +=
                        static_cast<UINT64>(pageIndex) *
                        atlasDescriptorSize;
                    commandList.SetGraphicsRootDescriptorTable(
                        0,
                        pageDescriptor);
                    commandList.SetGraphicsRootShaderResourceView(
                        1,
                        frame.resource->GetGPUVirtualAddress() +
                            static_cast<UINT64>(firstInstance) *
                            sizeof(GpuGlyphInstance));
                    commandList.DrawInstanced(
                        6,
                        static_cast<UINT>(endInstance - firstInstance),
                        0,
                        0);
                    firstInstance = endInstance;
                }
            }

            pendingInstances.clear();
            pendingUploads.clear();
            frameOpen = false;
        }

        void AppendGlyphRun(
            const float baselineOriginX,
            const float baselineOriginY,
            const DWRITE_MEASURING_MODE measuringMode,
            const DWRITE_GLYPH_RUN& glyphRun,
            const DirectX::XMFLOAT4& color,
            const float depth,
            const DirectX::XMFLOAT4X4& transform)
        {
            float penX = baselineOriginX;
            const bool rightToLeft = (glyphRun.bidiLevel & 1U) != 0;
            for (UINT32 index = 0; index < glyphRun.glyphCount; ++index)
            {
                const DWRITE_GLYPH_OFFSET offset =
                    glyphRun.glyphOffsets != nullptr
                    ? glyphRun.glyphOffsets[index]
                    : DWRITE_GLYPH_OFFSET{};
                const GlyphEntry& glyph = FindOrCreateGlyph(
                    *glyphRun.fontFace,
                    glyphRun.glyphIndices[index],
                    glyphRun.fontEmSize,
                    measuringMode);

                if (glyph.drawable)
                {
                    pendingInstances.push_back(GlyphInstance{
                        transform,
                        {
                            penX + offset.advanceOffset +
                                glyph.bearingPixels.x,
                            baselineOriginY - offset.ascenderOffset +
                                glyph.bearingPixels.y},
                        glyph.sizePixels,
                        glyph.uvRectangle,
                        color,
                        depth,
                        glyph.pageIndex});
                }

                const float advance =
                    glyphRun.glyphAdvances != nullptr
                    ? glyphRun.glyphAdvances[index]
                    : 0.0F;
                penX += rightToLeft ? -advance : advance;
            }
        }

        [[nodiscard]] ComPtr<IDWriteTextLayout> FindOrCreateLayout(
            const std::wstring_view text,
            const TextDrawCommand& command)
        {
            LayoutKey key{
                command.style.font,
                std::wstring(text),
                std::bit_cast<std::uint32_t>(
                    command.style.fontSizePixels),
                std::bit_cast<std::uint32_t>(
                    command.layoutSizePixels.x),
                std::bit_cast<std::uint32_t>(
                    command.layoutSizePixels.y),
                command.horizontalAlignment,
                command.verticalAlignment};
            if (const auto found = layoutCache.find(key);
                found != layoutCache.end())
            {
                return found->second;
            }

            if (layoutCache.size() >= 128)
            {
                layoutCache.clear();
            }

            const Font::Impl& font =
                *command.style.font->implementation_;
            ComPtr<IDWriteTextFormat> format;
            ThrowIfFailed(
                directWriteFactory->CreateTextFormat(
                    font.familyName.c_str(),
                    font.collection.Get(),
                    DWRITE_FONT_WEIGHT_NORMAL,
                    DWRITE_FONT_STYLE_NORMAL,
                    DWRITE_FONT_STRETCH_NORMAL,
                    command.style.fontSizePixels,
                    L"ko-kr",
                    format.ReleaseAndGetAddressOf()),
                "IDWriteFactory::CreateTextFormat");
            ThrowIfFailed(
                format->SetTextAlignment(
                    ToDirectWrite(command.horizontalAlignment)),
                "IDWriteTextFormat::SetTextAlignment");
            ThrowIfFailed(
                format->SetParagraphAlignment(
                    ToDirectWrite(command.verticalAlignment)),
                "IDWriteTextFormat::SetParagraphAlignment");
            ThrowIfFailed(
                format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP),
                "IDWriteTextFormat::SetWordWrapping");

            ComPtr<IDWriteTextLayout> layout;
            ThrowIfFailed(
                directWriteFactory->CreateTextLayout(
                    key.text.c_str(),
                    static_cast<UINT32>(key.text.size()),
                    format.Get(),
                    command.layoutSizePixels.x,
                    command.layoutSizePixels.y,
                    layout.ReleaseAndGetAddressOf()),
                "IDWriteFactory::CreateTextLayout");
            layoutCache.emplace(std::move(key), layout);
            return layout;
        }

        [[nodiscard]] const GlyphEntry& FindOrCreateGlyph(
            IDWriteFontFace& fontFace,
            const std::uint16_t glyphIndex,
            const float fontEmSize,
            const DWRITE_MEASURING_MODE measuringMode)
        {
            ComPtr<IUnknown> identity;
            ThrowIfFailed(
                fontFace.QueryInterface(
                    __uuidof(IUnknown),
                    reinterpret_cast<void**>(
                        identity.ReleaseAndGetAddressOf())),
                "IDWriteFontFace::QueryInterface");
            const GlyphKey key{
                reinterpret_cast<std::uintptr_t>(identity.Get()),
                std::bit_cast<std::uint32_t>(fontEmSize),
                glyphIndex,
                measuringMode};
            if (const auto found = glyphCache.find(key);
                found != glyphCache.end())
            {
                return found->second;
            }

            // Rasterize exactly one glyph. Its font-face COM identity is part
            // of the cache key, so equal glyph indices from different fonts
            // never share an atlas entry accidentally.
            const float zeroAdvance = 0.0F;
            const DWRITE_GLYPH_OFFSET zeroOffset{};
            DWRITE_GLYPH_RUN singleGlyphRun{};
            singleGlyphRun.fontFace = &fontFace;
            singleGlyphRun.fontEmSize = fontEmSize;
            singleGlyphRun.glyphCount = 1;
            singleGlyphRun.glyphIndices = &glyphIndex;
            singleGlyphRun.glyphAdvances = &zeroAdvance;
            singleGlyphRun.glyphOffsets = &zeroOffset;

            ComPtr<IDWriteGlyphRunAnalysis> analysis;
            // DirectWrite emits the antialiased texture requested below only
            // when the analysis uses the matching ClearType mode. The three
            // channel coverages are collapsed to one R8 value before upload,
            // so the final D3D12 composition remains color-neutral.
            ThrowIfFailed(
                directWriteFactory->CreateGlyphRunAnalysis(
                    &singleGlyphRun,
                    nullptr,
                    DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC,
                    measuringMode,
                    DWRITE_GRID_FIT_MODE_ENABLED,
                    DWRITE_TEXT_ANTIALIAS_MODE_CLEARTYPE,
                    0.0F,
                    0.0F,
                    analysis.ReleaseAndGetAddressOf()),
                "IDWriteFactory3::CreateGlyphRunAnalysis");

            RECT bounds{};
            ThrowIfFailed(
                analysis->GetAlphaTextureBounds(
                    DWRITE_TEXTURE_CLEARTYPE_3x1,
                    &bounds),
                "IDWriteGlyphRunAnalysis::GetAlphaTextureBounds");
            const std::uint32_t width = static_cast<std::uint32_t>(
                std::max<LONG>(bounds.right - bounds.left, 0));
            const std::uint32_t height = static_cast<std::uint32_t>(
                std::max<LONG>(bounds.bottom - bounds.top, 0));

            GlyphEntry entry;
            entry.fontFaceIdentity = identity;
            entry.sizePixels = {
                static_cast<float>(width),
                static_cast<float>(height)};
            entry.bearingPixels = {
                static_cast<float>(bounds.left),
                static_cast<float>(bounds.top)};
            entry.drawable = width > 0 && height > 0;
            if (entry.drawable)
            {
                // DirectWrite returns three ClearType coverage channels. The
                // renderer stores their maximum as a color-neutral R8 mask.
                std::vector<std::uint8_t> clearTypePixels(
                    static_cast<std::size_t>(width) * height * 3U);
                ThrowIfFailed(
                    analysis->CreateAlphaTexture(
                        DWRITE_TEXTURE_CLEARTYPE_3x1,
                        &bounds,
                        clearTypePixels.data(),
                        static_cast<UINT32>(clearTypePixels.size())),
                    "IDWriteGlyphRunAnalysis::CreateAlphaTexture");

                const std::uint32_t uploadWidth =
                    width + GlyphPadding * 2U;
                const std::uint32_t uploadHeight =
                    height + GlyphPadding * 2U;
                std::vector<std::uint8_t> coverage(
                    static_cast<std::size_t>(uploadWidth) * uploadHeight,
                    0U);
                for (std::uint32_t y = 0; y < height; ++y)
                {
                    for (std::uint32_t x = 0; x < width; ++x)
                    {
                        const std::size_t sourceIndex =
                            (static_cast<std::size_t>(y) * width + x) * 3U;
                        const std::uint8_t alpha = std::max({
                            clearTypePixels[sourceIndex],
                            clearTypePixels[sourceIndex + 1U],
                            clearTypePixels[sourceIndex + 2U]});
                        coverage[
                            static_cast<std::size_t>(
                                y + GlyphPadding) * uploadWidth +
                            x + GlyphPadding] = alpha;
                    }
                }

                std::uint32_t pageIndex = 0;
                std::uint32_t destinationX = 0;
                std::uint32_t destinationY = 0;
                AllocateAtlasRegion(
                    uploadWidth,
                    uploadHeight,
                    pageIndex,
                    destinationX,
                    destinationY);
                entry.pageIndex = pageIndex;

                // Padding prevents filtering from sampling an adjacent glyph
                // when the UV rectangle reaches an atlas-cell edge.
                const float left = static_cast<float>(
                    destinationX + GlyphPadding);
                const float top = static_cast<float>(
                    destinationY + GlyphPadding);
                entry.uvRectangle = {
                    left / static_cast<float>(AtlasWidth),
                    top / static_cast<float>(AtlasHeight),
                    (left + static_cast<float>(width)) /
                        static_cast<float>(AtlasWidth),
                    (top + static_cast<float>(height)) /
                        static_cast<float>(AtlasHeight)};
                pendingUploads.push_back(PendingGlyphUpload{
                    pageIndex,
                    destinationX,
                    destinationY,
                    uploadWidth,
                    uploadHeight,
                    std::move(coverage)});
            }

            return glyphCache.emplace(key, std::move(entry)).first->second;
        }

        void AllocateAtlasRegion(
            const std::uint32_t width,
            const std::uint32_t height,
            std::uint32_t& pageIndex,
            std::uint32_t& destinationX,
            std::uint32_t& destinationY)
        {
            if (width > AtlasWidth || height > AtlasHeight)
            {
                throw std::runtime_error(
                    "A rasterized glyph is larger than the glyph atlas.");
            }

            for (std::uint32_t index = 0;
                 index < atlasPages.size();
                 ++index)
            {
                if (TryAllocate(
                        atlasPages[index],
                        width,
                        height,
                        destinationX,
                        destinationY))
                {
                    pageIndex = index;
                    return;
                }
            }

            if (atlasPages.size() >= MaximumAtlasPages)
            {
                throw std::runtime_error(
                    "The text glyph atlas reached its page limit.");
            }
            CreateAtlasPage();
            pageIndex =
                static_cast<std::uint32_t>(atlasPages.size() - 1U);
            if (!TryAllocate(
                    atlasPages.back(),
                    width,
                    height,
                    destinationX,
                    destinationY))
            {
                throw std::runtime_error(
                    "Failed to allocate a glyph in a new atlas page.");
            }
        }

        [[nodiscard]] bool TryAllocate(
            AtlasPage& page,
            const std::uint32_t width,
            const std::uint32_t height,
            std::uint32_t& destinationX,
            std::uint32_t& destinationY) noexcept
        {
            if (page.nextX + width + GlyphPadding > AtlasWidth)
            {
                page.nextX = GlyphPadding;
                page.nextY += page.rowHeight + GlyphPadding;
                page.rowHeight = 0;
            }
            if (page.nextY + height + GlyphPadding > AtlasHeight)
            {
                return false;
            }

            destinationX = page.nextX;
            destinationY = page.nextY;
            page.nextX += width + GlyphPadding;
            page.rowHeight = std::max(page.rowHeight, height);
            return true;
        }

        void CreateAtlasPage()
        {
            D3D12_RESOURCE_DESC description{};
            description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            description.Width = AtlasWidth;
            description.Height = AtlasHeight;
            description.DepthOrArraySize = 1;
            description.MipLevels = 1;
            description.Format = DXGI_FORMAT_R8_UNORM;
            description.SampleDesc.Count = 1;
            description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

            AtlasPage page;
            const D3D12_HEAP_PROPERTIES properties =
                HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
            ThrowIfFailed(
                device->CreateCommittedResource(
                    &properties,
                    D3D12_HEAP_FLAG_NONE,
                    &description,
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                    nullptr,
                    IID_PPV_ARGS(page.texture.ReleaseAndGetAddressOf())),
                "ID3D12Device::CreateCommittedResource(glyph atlas)");

            D3D12_SHADER_RESOURCE_VIEW_DESC shaderResourceView{};
            shaderResourceView.Format = DXGI_FORMAT_R8_UNORM;
            shaderResourceView.ViewDimension =
                D3D12_SRV_DIMENSION_TEXTURE2D;
            shaderResourceView.Shader4ComponentMapping =
                D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            shaderResourceView.Texture2D.MipLevels = 1;

            D3D12_CPU_DESCRIPTOR_HANDLE descriptor =
                atlasDescriptorHeap->
                    GetCPUDescriptorHandleForHeapStart();
            descriptor.ptr +=
                static_cast<SIZE_T>(atlasPages.size()) *
                atlasDescriptorSize;
            device->CreateShaderResourceView(
                page.texture.Get(),
                &shaderResourceView,
                descriptor);
            atlasPages.push_back(std::move(page));
        }

        void UploadNewGlyphs(ID3D12GraphicsCommandList& commandList)
        {
            if (pendingUploads.empty())
            {
                return;
            }

            // Transition only pages touched by this frame's pending uploads.
            std::array<bool, MaximumAtlasPages> touchedPages{};
            for (const PendingGlyphUpload& upload : pendingUploads)
            {
                touchedPages[upload.pageIndex] = true;
            }

            std::vector<D3D12_RESOURCE_BARRIER> toCopy;
            for (std::uint32_t pageIndex = 0;
                 pageIndex < atlasPages.size();
                 ++pageIndex)
            {
                if (touchedPages[pageIndex])
                {
                    toCopy.push_back(TransitionBarrier(
                        *atlasPages[pageIndex].texture.Get(),
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_COPY_DEST));
                }
            }
            commandList.ResourceBarrier(
                static_cast<UINT>(toCopy.size()),
                toCopy.data());

            FrameInstanceBuffer& frame =
                frameBuffers[currentFrameIndex];
            for (const PendingGlyphUpload& upload : pendingUploads)
            {
                // Each small upload resource is retained by the current frame
                // until its fence proves that the GPU copy has completed.
                const std::uint32_t rowPitch = Align(
                    upload.width,
                    D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
                const std::size_t uploadSize =
                    static_cast<std::size_t>(rowPitch) * upload.height;

                ComPtr<ID3D12Resource> uploadBuffer;
                const D3D12_HEAP_PROPERTIES uploadProperties =
                    HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
                const D3D12_RESOURCE_DESC bufferDescription =
                    BufferDescription(uploadSize);
                ThrowIfFailed(
                    device->CreateCommittedResource(
                        &uploadProperties,
                        D3D12_HEAP_FLAG_NONE,
                        &bufferDescription,
                        D3D12_RESOURCE_STATE_GENERIC_READ,
                        nullptr,
                        IID_PPV_ARGS(
                            uploadBuffer.ReleaseAndGetAddressOf())),
                    "ID3D12Device::CreateCommittedResource(glyph upload)");

                std::byte* mapped = nullptr;
                ThrowIfFailed(
                    uploadBuffer->Map(
                        0,
                        nullptr,
                        reinterpret_cast<void**>(&mapped)),
                    "ID3D12Resource::Map(glyph upload)");
                for (std::uint32_t y = 0; y < upload.height; ++y)
                {
                    std::memcpy(
                        mapped + static_cast<std::size_t>(y) * rowPitch,
                        upload.pixels.data() +
                            static_cast<std::size_t>(y) * upload.width,
                        upload.width);
                }
                uploadBuffer->Unmap(0, nullptr);

                D3D12_TEXTURE_COPY_LOCATION destination{};
                destination.pResource =
                    atlasPages[upload.pageIndex].texture.Get();
                destination.Type =
                    D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                destination.SubresourceIndex = 0;

                D3D12_TEXTURE_COPY_LOCATION source{};
                source.pResource = uploadBuffer.Get();
                source.Type =
                    D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                source.PlacedFootprint.Offset = 0;
                source.PlacedFootprint.Footprint.Format =
                    DXGI_FORMAT_R8_UNORM;
                source.PlacedFootprint.Footprint.Width = upload.width;
                source.PlacedFootprint.Footprint.Height = upload.height;
                source.PlacedFootprint.Footprint.Depth = 1;
                source.PlacedFootprint.Footprint.RowPitch = rowPitch;

                commandList.CopyTextureRegion(
                    &destination,
                    upload.destinationX,
                    upload.destinationY,
                    0,
                    &source,
                    nullptr);
                frame.uploadResources.push_back(std::move(uploadBuffer));
            }

            std::vector<D3D12_RESOURCE_BARRIER> toShaderResource;
            for (std::uint32_t pageIndex = 0;
                 pageIndex < atlasPages.size();
                 ++pageIndex)
            {
                if (touchedPages[pageIndex])
                {
                    toShaderResource.push_back(TransitionBarrier(
                        *atlasPages[pageIndex].texture.Get(),
                        D3D12_RESOURCE_STATE_COPY_DEST,
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
                }
            }
            commandList.ResourceBarrier(
                static_cast<UINT>(toShaderResource.size()),
                toShaderResource.data());
        }

        void EnsureInstanceCapacity(const std::size_t requiredCapacity)
        {
            FrameInstanceBuffer& frame =
                frameBuffers[currentFrameIndex];
            if (frame.capacity >= requiredCapacity)
            {
                return;
            }

            if (frame.resource != nullptr && frame.mappedData != nullptr)
            {
                frame.resource->Unmap(0, nullptr);
            }
            frame.resource.Reset();
            frame.mappedData = nullptr;

            std::size_t capacity = std::max<std::size_t>(256, frame.capacity);
            while (capacity < requiredCapacity)
            {
                capacity *= 2;
            }
            const std::size_t sizeBytes =
                capacity * sizeof(GpuGlyphInstance);
            const D3D12_HEAP_PROPERTIES properties =
                HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
            const D3D12_RESOURCE_DESC description =
                BufferDescription(sizeBytes);
            ThrowIfFailed(
                device->CreateCommittedResource(
                    &properties,
                    D3D12_HEAP_FLAG_NONE,
                    &description,
                    D3D12_RESOURCE_STATE_GENERIC_READ,
                    nullptr,
                    IID_PPV_ARGS(
                        frame.resource.ReleaseAndGetAddressOf())),
                "ID3D12Device::CreateCommittedResource(text instances)");
            ThrowIfFailed(
                frame.resource->Map(
                    0,
                    nullptr,
                    reinterpret_cast<void**>(&frame.mappedData)),
                "ID3D12Resource::Map(text instances)");
            frame.capacity = capacity;
        }

        void CreateDescriptorHeap()
        {
            D3D12_DESCRIPTOR_HEAP_DESC description{};
            description.Type =
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            description.NumDescriptors = MaximumAtlasPages;
            description.Flags =
                D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            ThrowIfFailed(
                device->CreateDescriptorHeap(
                    &description,
                    IID_PPV_ARGS(
                        atlasDescriptorHeap.ReleaseAndGetAddressOf())),
                "ID3D12Device::CreateDescriptorHeap(text atlas)");
            atlasDescriptorSize =
                device->GetDescriptorHandleIncrementSize(
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }

        void CreatePipeline(const DXGI_FORMAT renderTargetFormat)
        {
            // Root parameters expose the current atlas page, the structured
            // glyph-instance range, and the viewport size to the shaders.
            D3D12_DESCRIPTOR_RANGE atlasRange{};
            atlasRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            atlasRange.NumDescriptors = 1;
            atlasRange.BaseShaderRegister = 0;
            atlasRange.RegisterSpace = 0;
            atlasRange.OffsetInDescriptorsFromTableStart = 0;

            std::array<D3D12_ROOT_PARAMETER, 3> parameters{};
            parameters[0].ParameterType =
                D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameters[0].DescriptorTable.NumDescriptorRanges = 1;
            parameters[0].DescriptorTable.pDescriptorRanges =
                &atlasRange;
            parameters[0].ShaderVisibility =
                D3D12_SHADER_VISIBILITY_PIXEL;

            parameters[1].ParameterType =
                D3D12_ROOT_PARAMETER_TYPE_SRV;
            parameters[1].Descriptor.ShaderRegister = 1;
            parameters[1].Descriptor.RegisterSpace = 0;
            parameters[1].ShaderVisibility =
                D3D12_SHADER_VISIBILITY_VERTEX;

            parameters[2].ParameterType =
                D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            parameters[2].Constants.ShaderRegister = 0;
            parameters[2].Constants.RegisterSpace = 0;
            parameters[2].Constants.Num32BitValues = 2;
            parameters[2].ShaderVisibility =
                D3D12_SHADER_VISIBILITY_VERTEX;

            D3D12_STATIC_SAMPLER_DESC sampler{};
            sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.MipLODBias = 0.0F;
            sampler.MaxAnisotropy = 1;
            sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            sampler.BorderColor =
                D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
            sampler.MinLOD = 0.0F;
            sampler.MaxLOD = FLT_MAX;
            sampler.ShaderRegister = 0;
            sampler.RegisterSpace = 0;
            sampler.ShaderVisibility =
                D3D12_SHADER_VISIBILITY_PIXEL;

            D3D12_ROOT_SIGNATURE_DESC rootDescription{};
            rootDescription.NumParameters =
                static_cast<UINT>(parameters.size());
            rootDescription.pParameters = parameters.data();
            rootDescription.NumStaticSamplers = 1;
            rootDescription.pStaticSamplers = &sampler;
            rootDescription.Flags =
                D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

            ComPtr<ID3DBlob> serialized;
            ComPtr<ID3DBlob> errors;
            const HRESULT serializationResult =
                D3D12SerializeRootSignature(
                    &rootDescription,
                    D3D_ROOT_SIGNATURE_VERSION_1,
                    serialized.ReleaseAndGetAddressOf(),
                    errors.ReleaseAndGetAddressOf());
            if (FAILED(serializationResult))
            {
                const std::string details = errors != nullptr
                    ? std::string(
                        static_cast<const char*>(
                            errors->GetBufferPointer()),
                        errors->GetBufferSize())
                    : "No root-signature diagnostics were returned.";
                throw std::runtime_error(
                    "Text root-signature serialization failed: " +
                    details);
            }
            ThrowIfFailed(
                device->CreateRootSignature(
                    0,
                    serialized->GetBufferPointer(),
                    serialized->GetBufferSize(),
                    IID_PPV_ARGS(
                        rootSignature.ReleaseAndGetAddressOf())),
                "ID3D12Device::CreateRootSignature(text)");

            const ComPtr<ID3DBlob> vertexShader =
                detail::CompileShader(
                    detail::BuiltInShader::Text,
                    "VSMain",
                    "vs_5_1");
            const ComPtr<ID3DBlob> pixelShader =
                detail::CompileShader(
                    detail::BuiltInShader::Text,
                    "PSMain",
                    "ps_5_1");

            D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
            pipeline.pRootSignature = rootSignature.Get();
            pipeline.VS = {
                vertexShader->GetBufferPointer(),
                vertexShader->GetBufferSize()};
            pipeline.PS = {
                pixelShader->GetBufferPointer(),
                pixelShader->GetBufferSize()};
            pipeline.BlendState.AlphaToCoverageEnable = FALSE;
            pipeline.BlendState.IndependentBlendEnable = FALSE;
            D3D12_RENDER_TARGET_BLEND_DESC& blend =
                pipeline.BlendState.RenderTarget[0];
            blend.BlendEnable = TRUE;
            blend.LogicOpEnable = FALSE;
            blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
            blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            blend.BlendOp = D3D12_BLEND_OP_ADD;
            blend.SrcBlendAlpha = D3D12_BLEND_ONE;
            blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
            blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            blend.LogicOp = D3D12_LOGIC_OP_NOOP;
            blend.RenderTargetWriteMask =
                D3D12_COLOR_WRITE_ENABLE_ALL;

            pipeline.SampleMask = UINT_MAX;
            pipeline.RasterizerState.FillMode =
                D3D12_FILL_MODE_SOLID;
            pipeline.RasterizerState.CullMode =
                D3D12_CULL_MODE_NONE;
            pipeline.RasterizerState.FrontCounterClockwise = FALSE;
            pipeline.RasterizerState.DepthBias =
                D3D12_DEFAULT_DEPTH_BIAS;
            pipeline.RasterizerState.DepthBiasClamp =
                D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
            pipeline.RasterizerState.SlopeScaledDepthBias =
                D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
            pipeline.RasterizerState.DepthClipEnable = TRUE;
            pipeline.RasterizerState.MultisampleEnable = FALSE;
            pipeline.RasterizerState.AntialiasedLineEnable = FALSE;
            pipeline.RasterizerState.ForcedSampleCount = 0;
            pipeline.RasterizerState.ConservativeRaster =
                D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
            pipeline.DepthStencilState.DepthEnable =
                depthStencilFormat != DXGI_FORMAT_UNKNOWN;
            pipeline.DepthStencilState.DepthWriteMask =
                D3D12_DEPTH_WRITE_MASK_ZERO;
            pipeline.DepthStencilState.DepthFunc =
                D3D12_COMPARISON_FUNC_LESS_EQUAL;
            pipeline.DepthStencilState.StencilEnable = FALSE;
            pipeline.InputLayout = {nullptr, 0};
            pipeline.PrimitiveTopologyType =
                D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            pipeline.NumRenderTargets = 1;
            pipeline.RTVFormats[0] = renderTargetFormat;
            pipeline.DSVFormat = depthStencilFormat;
            pipeline.SampleDesc.Count = 1;

            ThrowIfFailed(
                device->CreateGraphicsPipelineState(
                    &pipeline,
                    IID_PPV_ARGS(
                        pipelineState.ReleaseAndGetAddressOf())),
                "ID3D12Device::CreateGraphicsPipelineState(text)");
        }

        void RequireInitialized() const
        {
            if (!initialized)
            {
                throw std::logic_error(
                    "TextRenderSystem is not initialized.");
            }
        }

        void RequireFrameOpen() const
        {
            RequireInitialized();
            if (!frameOpen)
            {
                throw std::logic_error(
                    "Text submission requires an open frame.");
            }
        }

        ID3D12Device* device{};
        DXGI_FORMAT depthStencilFormat{DXGI_FORMAT_UNKNOWN};
        ComPtr<IDWriteFactory5> directWriteFactory;
        ComPtr<ID3D12DescriptorHeap> atlasDescriptorHeap;
        ComPtr<ID3D12RootSignature> rootSignature;
        ComPtr<ID3D12PipelineState> pipelineState;
        std::uint32_t atlasDescriptorSize{};
        std::vector<AtlasPage> atlasPages;
        std::unordered_map<GlyphKey, GlyphEntry, GlyphKeyHash> glyphCache;
        std::unordered_map<
            LayoutKey,
            ComPtr<IDWriteTextLayout>,
            LayoutKeyHash> layoutCache;
        std::vector<PendingGlyphUpload> pendingUploads;
        std::vector<GlyphInstance> pendingInstances;
        std::array<FrameInstanceBuffer, FrameCount> frameBuffers{};
        std::uint32_t currentFrameIndex{};
        bool initialized{};
        bool frameOpen{};
    };

    TextRenderSystem::TextRenderSystem()
        : implementation_(std::make_unique<Impl>())
    {
    }

    TextRenderSystem::~TextRenderSystem()
    {
        Shutdown();
    }

    void TextRenderSystem::Initialize(
        ID3D12Device& device,
        const DXGI_FORMAT renderTargetFormat,
        const DXGI_FORMAT depthStencilFormat)
    {
        implementation_->Initialize(
            device,
            renderTargetFormat,
            depthStencilFormat);
    }

    void TextRenderSystem::Shutdown() noexcept
    {
        implementation_->Shutdown();
    }

    FontHandle TextRenderSystem::LoadSystemFont(
        const std::wstring_view familyName)
    {
        return implementation_->LoadSystemFont(familyName);
    }

    FontHandle TextRenderSystem::LoadFontFile(
        const std::filesystem::path& fontFile)
    {
        return implementation_->LoadFontFile(fontFile);
    }

    void TextRenderSystem::BeginFrame(const std::uint32_t frameIndex)
    {
        implementation_->BeginFrame(frameIndex);
    }

    void TextRenderSystem::Submit(
        const std::wstring_view text,
        const TextDrawCommand& command)
    {
        implementation_->Submit(text, command);
    }

    void TextRenderSystem::Flush(
        ID3D12GraphicsCommandList& commandList,
        const std::uint32_t viewportWidth,
        const std::uint32_t viewportHeight)
    {
        implementation_->Flush(
            commandList,
            viewportWidth,
            viewportHeight);
    }
}
