#include "Texture/TextureManager.h"

#include <Windows.h>
#include <wincodec.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;

namespace mrg::graphics
{
    namespace
    {
        constexpr std::uint32_t DescriptorHeapCapacity = 1024;

        void ThrowIfFailed(const HRESULT result, const char* operation)
        {
            if (FAILED(result))
            {
                throw std::runtime_error(
                    std::string(operation) + " failed with HRESULT " +
                    std::to_string(static_cast<long>(result)) + ".");
            }
        }

        [[nodiscard]] D3D12_RESOURCE_DESC BufferDescription(
            const std::uint64_t byteCount) noexcept
        {
            D3D12_RESOURCE_DESC description{};
            description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            description.Width = byteCount;
            description.Height = 1;
            description.DepthOrArraySize = 1;
            description.MipLevels = 1;
            description.SampleDesc.Count = 1;
            description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            return description;
        }

        [[nodiscard]] D3D12_RESOURCE_DESC TextureDescription(
            const std::uint32_t width,
            const std::uint32_t height) noexcept
        {
            D3D12_RESOURCE_DESC description{};
            description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            description.Width = width;
            description.Height = height;
            description.DepthOrArraySize = 1;
            description.MipLevels = 1;
            description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            description.SampleDesc.Count = 1;
            description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            return description;
        }

        [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE OffsetCpuDescriptor(
            D3D12_CPU_DESCRIPTOR_HANDLE start,
            const std::uint32_t index,
            const std::uint32_t descriptorSize) noexcept
        {
            start.ptr += static_cast<SIZE_T>(index) * descriptorSize;
            return start;
        }

        [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE OffsetGpuDescriptor(
            D3D12_GPU_DESCRIPTOR_HANDLE start,
            const std::uint32_t index,
            const std::uint32_t descriptorSize) noexcept
        {
            start.ptr += static_cast<UINT64>(index) * descriptorSize;
            return start;
        }

    }

    struct TextureManager::DecodedImage final
    {
        std::uint32_t width{};
        std::uint32_t height{};
        std::vector<std::byte> pixels;
    };

    std::size_t TextureSet::Size() const noexcept
    {
        return textureInfo_.size();
    }

    const TextureInfo& TextureSet::Info(const std::size_t index) const
    {
        if (index >= textureInfo_.size())
        {
            throw std::out_of_range("TextureSet index is out of range.");
        }
        return textureInfo_[index];
    }

    UvTransform TextureSet::MakeCoverUvTransform(
        const std::size_t index,
        const float targetAspectRatio) const
    {
        if (targetAspectRatio <= 0.0F)
        {
            throw std::invalid_argument(
                "The target UV aspect ratio must be positive.");
        }

        const TextureInfo& info = Info(index);
        const float sourceAspectRatio =
            static_cast<float>(info.width) /
            static_cast<float>(info.height);

        UvTransform transform{};
        if (sourceAspectRatio > targetAspectRatio)
        {
            transform.scale.x =
                targetAspectRatio / sourceAspectRatio;
            transform.offset.x =
                (1.0F - transform.scale.x) * 0.5F;
        }
        else if (sourceAspectRatio < targetAspectRatio)
        {
            transform.scale.y =
                sourceAspectRatio / targetAspectRatio;
            transform.offset.y =
                (1.0F - transform.scale.y) * 0.5F;
        }
        return transform;
    }

    const TextureSetHandle& RenderTargetTexture::Textures() const noexcept
    {
        return textures_;
    }

    std::uint32_t RenderTargetTexture::Width() const noexcept
    {
        return width_;
    }

    std::uint32_t RenderTargetTexture::Height() const noexcept
    {
        return height_;
    }

    TextureManager::~TextureManager()
    {
        Shutdown();
    }

    void TextureManager::Initialize(
        ID3D12Device& device,
        ID3D12CommandQueue& commandQueue)
    {
        if (initialized_)
        {
            throw std::logic_error(
                "TextureManager is already initialized.");
        }

        device_ = &device;
        commandQueue_ = &commandQueue;
        try
        {
            D3D12_DESCRIPTOR_HEAP_DESC heapDescription{};
            heapDescription.Type =
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            heapDescription.NumDescriptors = DescriptorHeapCapacity;
            heapDescription.Flags =
                D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            ThrowIfFailed(
                device.CreateDescriptorHeap(
                    &heapDescription,
                    IID_PPV_ARGS(
                        descriptorHeap_.ReleaseAndGetAddressOf())),
                "Create texture descriptor heap");
            descriptorSize_ = device.GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            ThrowIfFailed(
                device.CreateCommandAllocator(
                    D3D12_COMMAND_LIST_TYPE_DIRECT,
                    IID_PPV_ARGS(
                        uploadAllocator_.ReleaseAndGetAddressOf())),
                "Create texture upload command allocator");
            ThrowIfFailed(
                device.CreateCommandList(
                    0,
                    D3D12_COMMAND_LIST_TYPE_DIRECT,
                    uploadAllocator_.Get(),
                    nullptr,
                    IID_PPV_ARGS(
                        uploadCommandList_.ReleaseAndGetAddressOf())),
                "Create texture upload command list");
            ThrowIfFailed(
                uploadCommandList_->Close(),
                "Close initial texture upload command list");

            ThrowIfFailed(
                device.CreateFence(
                    0,
                    D3D12_FENCE_FLAG_NONE,
                    IID_PPV_ARGS(uploadFence_.ReleaseAndGetAddressOf())),
                "Create texture upload fence");
            uploadFenceEvent_ =
                CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (uploadFenceEvent_ == nullptr)
            {
                throw std::runtime_error(
                    "CreateEventW failed for texture uploads.");
            }

            initialized_ = true;
        }
        catch (...)
        {
            Shutdown();
            throw;
        }
    }

    void TextureManager::Shutdown() noexcept
    {
        if (uploadFenceEvent_ != nullptr)
        {
            CloseHandle(uploadFenceEvent_);
            uploadFenceEvent_ = nullptr;
        }

        uploadFence_.Reset();
        uploadCommandList_.Reset();
        uploadAllocator_.Reset();
        descriptorHeap_.Reset();
        device_ = nullptr;
        commandQueue_ = nullptr;
        nextUploadFenceValue_ = 1;
        descriptorSize_ = 0;
        nextDescriptorBlock_ = 0;
        initialized_ = false;
    }

    TextureSetHandle TextureManager::LoadTextureSet(
        const std::span<const std::filesystem::path> paths)
    {
        ValidateTextureSetRequest(paths);

        // Decode first so a bad file cannot leave an open D3D12 upload list.
        std::vector<DecodedImage> decodedImages;
        decodedImages.reserve(paths.size());
        for (const std::filesystem::path& path : paths)
        {
            decodedImages.push_back(DecodeImage(path));
        }

        BeginUploadCommands();
        std::shared_ptr<TextureSet> textureSet{new TextureSet()};
        textureSet->textureInfo_.reserve(decodedImages.size());
        textureSet->resources_.reserve(decodedImages.size());
        std::vector<ComPtr<ID3D12Resource>> uploadBuffers;
        uploadBuffers.reserve(decodedImages.size());
        const D3D12_CPU_DESCRIPTOR_HANDLE cpuBlockStart =
            InitializeDescriptorBlock(*textureSet);

        try
        {
            for (std::size_t index = 0;
                 index < decodedImages.size();
                 ++index)
            {
                RecordTextureUpload(
                    decodedImages[index],
                    index,
                    cpuBlockStart,
                    *textureSet,
                    uploadBuffers);
            }

            ThrowIfFailed(
                uploadCommandList_->Close(),
                "Close texture upload command list");
        }
        catch (...)
        {
            // The list was never submitted, so closing it makes a later Reset
            // legal without requiring a fence.
            uploadCommandList_->Close();
            throw;
        }

        ExecuteUploadAndWait();
        nextDescriptorBlock_ += MaxTexturesPerSet;
        return textureSet;
    }

    TextureSetHandle TextureManager::AppendTexture(
        const TextureSetHandle& textureSet,
        const std::filesystem::path& path)
    {
        if (!initialized_ || device_ == nullptr || commandQueue_ == nullptr)
        {
            throw std::logic_error(
                "TextureManager must be initialized before appending images.");
        }
        if (textureSet == nullptr || path.empty())
        {
            throw std::invalid_argument(
                "Appending a texture requires an existing set and path.");
        }
        if (textureSet->Size() >= MaxTexturesPerSet)
        {
            throw std::length_error(
                "A texture descriptor table cannot contain more than 64 images.");
        }

        // Decode before opening the upload list so invalid user assets cannot
        // leave the reusable allocator in a recording state.
        const DecodedImage decodedImage = DecodeImage(path);
        std::shared_ptr<TextureSet> mutableSet =
            std::const_pointer_cast<TextureSet>(textureSet);
        BeginUploadCommands();
        std::vector<ComPtr<ID3D12Resource>> uploadBuffers;
        uploadBuffers.reserve(1);

        try
        {
            RecordTextureUpload(
                decodedImage,
                mutableSet->Size(),
                mutableSet->cpuDescriptorStart_,
                *mutableSet,
                uploadBuffers);
            ThrowIfFailed(
                uploadCommandList_->Close(),
                "Close appended texture upload command list");
        }
        catch (...)
        {
            uploadCommandList_->Close();
            throw;
        }

        ExecuteUploadAndWait();
        return textureSet;
    }

    RenderTargetTextureHandle TextureManager::CreateRenderTargetTexture(
        const std::uint32_t width,
        const std::uint32_t height)
    {
        if (!initialized_ || device_ == nullptr ||
            width == 0 || height == 0)
        {
            throw std::invalid_argument(
                "A render-target texture requires an initialized manager "
                "and a positive size.");
        }
        if (nextDescriptorBlock_ >
            DescriptorHeapCapacity - MaxTexturesPerSet)
        {
            throw std::runtime_error(
                "The texture descriptor heap has no free material block.");
        }

        auto target = std::shared_ptr<RenderTargetTexture>(
            new RenderTargetTexture());
        auto textureSet = std::shared_ptr<TextureSet>(new TextureSet());
        const D3D12_CPU_DESCRIPTOR_HANDLE descriptorBlock =
            InitializeDescriptorBlock(*textureSet);

        D3D12_RESOURCE_DESC description = TextureDescription(width, height);
        description.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_HEAP_PROPERTIES defaultHeap{};
        defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_CLEAR_VALUE clearValue{};
        clearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        ThrowIfFailed(
            device_->CreateCommittedResource(
                &defaultHeap,
                D3D12_HEAP_FLAG_NONE,
                &description,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                &clearValue,
                IID_PPV_ARGS(target->resource_.ReleaseAndGetAddressOf())),
            "Create render-target texture");

        D3D12_SHADER_RESOURCE_VIEW_DESC shaderView{};
        shaderView.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        shaderView.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        shaderView.Shader4ComponentMapping =
            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        shaderView.Texture2D.MipLevels = 1;
        device_->CreateShaderResourceView(
            target->resource_.Get(),
            &shaderView,
            descriptorBlock);

        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDescription{};
        rtvHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDescription.NumDescriptors = 1;
        ThrowIfFailed(
            device_->CreateDescriptorHeap(
                &rtvHeapDescription,
                IID_PPV_ARGS(target->rtvHeap_.ReleaseAndGetAddressOf())),
            "Create render-target texture RTV heap");
        target->rtv_ =
            target->rtvHeap_->GetCPUDescriptorHandleForHeapStart();
        device_->CreateRenderTargetView(
            target->resource_.Get(),
            nullptr,
            target->rtv_);

        textureSet->textureInfo_.push_back({width, height});
        textureSet->resources_.push_back(target->resource_);
        target->textures_ = textureSet;
        target->width_ = width;
        target->height_ = height;
        nextDescriptorBlock_ += MaxTexturesPerSet;
        return target;
    }

    void TextureManager::ValidateTextureSetRequest(
        const std::span<const std::filesystem::path> paths) const
    {
        if (!initialized_ || device_ == nullptr || commandQueue_ == nullptr)
        {
            throw std::logic_error(
                "TextureManager must be initialized before loading images.");
        }
        if (paths.empty() || paths.size() > MaxTexturesPerSet)
        {
            throw std::invalid_argument(
                "A texture set must contain between 1 and 64 images.");
        }
        if (nextDescriptorBlock_ >
            DescriptorHeapCapacity - MaxTexturesPerSet)
        {
            throw std::runtime_error(
                "The texture descriptor heap has no free material block.");
        }
    }

    TextureManager::DecodedImage TextureManager::DecodeImage(
        const std::filesystem::path& path)
    {
        ComPtr<IWICImagingFactory> factory;
        ThrowIfFailed(
            CoCreateInstance(
                CLSID_WICImagingFactory2,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(factory.ReleaseAndGetAddressOf())),
            "Create WIC imaging factory");

        ComPtr<IWICBitmapDecoder> decoder;
        ThrowIfFailed(
            factory->CreateDecoderFromFilename(
                path.c_str(),
                nullptr,
                GENERIC_READ,
                WICDecodeMetadataCacheOnLoad,
                decoder.ReleaseAndGetAddressOf()),
            "Open texture image");

        ComPtr<IWICBitmapFrameDecode> frame;
        ThrowIfFailed(
            decoder->GetFrame(0, frame.ReleaseAndGetAddressOf()),
            "Read texture frame");

        UINT width = 0;
        UINT height = 0;
        ThrowIfFailed(frame->GetSize(&width, &height), "Read texture dimensions");
        if (width == 0 || height == 0 ||
            width > std::numeric_limits<UINT>::max() / 4)
        {
            throw std::runtime_error(
                "The texture dimensions are empty or too large.");
        }

        ComPtr<IWICFormatConverter> converter;
        ThrowIfFailed(
            factory->CreateFormatConverter(
                converter.ReleaseAndGetAddressOf()),
            "Create WIC format converter");
        ThrowIfFailed(
            converter->Initialize(
                frame.Get(),
                GUID_WICPixelFormat32bppRGBA,
                WICBitmapDitherTypeNone,
                nullptr,
                0.0,
                WICBitmapPaletteTypeCustom),
            "Convert texture to RGBA8");

        const UINT stride = width * 4;
        const std::uint64_t pixelByteCount =
            static_cast<std::uint64_t>(stride) * height;
        if (pixelByteCount > std::numeric_limits<UINT>::max() ||
            pixelByteCount > std::numeric_limits<std::size_t>::max())
        {
            throw std::runtime_error(
                "The decoded texture exceeds WIC buffer limits.");
        }

        DecodedImage image{};
        image.width = width;
        image.height = height;
        image.pixels.resize(static_cast<std::size_t>(pixelByteCount));
        ThrowIfFailed(
            converter->CopyPixels(
                nullptr,
                stride,
                static_cast<UINT>(pixelByteCount),
                reinterpret_cast<BYTE*>(image.pixels.data())),
            "Copy decoded texture pixels");
        return image;
    }

    void TextureManager::BeginUploadCommands()
    {
        ThrowIfFailed(
            uploadAllocator_->Reset(),
            "Reset texture upload command allocator");
        ThrowIfFailed(
            uploadCommandList_->Reset(uploadAllocator_.Get(), nullptr),
            "Reset texture upload command list");
    }

    D3D12_CPU_DESCRIPTOR_HANDLE TextureManager::InitializeDescriptorBlock(
        TextureSet& textureSet)
    {
        const D3D12_CPU_DESCRIPTOR_HANDLE cpuBlockStart =
            OffsetCpuDescriptor(
                descriptorHeap_->GetCPUDescriptorHandleForHeapStart(),
                nextDescriptorBlock_,
                descriptorSize_);
        textureSet.cpuDescriptorStart_ = cpuBlockStart;
        textureSet.gpuDescriptorStart_ = OffsetGpuDescriptor(
            descriptorHeap_->GetGPUDescriptorHandleForHeapStart(),
            nextDescriptorBlock_,
            descriptorSize_);

        // Fill the fixed-size shader array with safe null SRVs first. Valid
        // images overwrite only the descriptors they use.
        D3D12_SHADER_RESOURCE_VIEW_DESC nullView{};
        nullView.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        nullView.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        nullView.Shader4ComponentMapping =
            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        nullView.Texture2D.MipLevels = 1;
        for (std::uint32_t index = 0; index < MaxTexturesPerSet; ++index)
        {
            device_->CreateShaderResourceView(
                nullptr,
                &nullView,
                OffsetCpuDescriptor(cpuBlockStart, index, descriptorSize_));
        }
        return cpuBlockStart;
    }

    void TextureManager::RecordTextureUpload(
        const DecodedImage& image,
        const std::size_t descriptorIndex,
        const D3D12_CPU_DESCRIPTOR_HANDLE descriptorBlockStart,
        TextureSet& textureSet,
        std::vector<ComPtr<ID3D12Resource>>& uploadBuffers)
    {
        // Allocate the default-heap destination and query the exact upload
        // footprint required by this image's row alignment.
        const D3D12_RESOURCE_DESC textureDescription =
            TextureDescription(image.width, image.height);
        D3D12_HEAP_PROPERTIES defaultHeap{};
        defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
        ComPtr<ID3D12Resource> texture;
        ThrowIfFailed(
            device_->CreateCommittedResource(
                &defaultHeap,
                D3D12_HEAP_FLAG_NONE,
                &textureDescription,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(texture.ReleaseAndGetAddressOf())),
            "Create GPU texture");

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT rowCount = 0;
        UINT64 sourceRowSize = 0;
        UINT64 uploadByteCount = 0;
        device_->GetCopyableFootprints(
            &textureDescription,
            0,
            1,
            0,
            &footprint,
            &rowCount,
            &sourceRowSize,
            &uploadByteCount);

        D3D12_HEAP_PROPERTIES uploadHeap{};
        uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
        const D3D12_RESOURCE_DESC uploadDescription =
            BufferDescription(uploadByteCount);
        ComPtr<ID3D12Resource> uploadBuffer;
        ThrowIfFailed(
            device_->CreateCommittedResource(
                &uploadHeap,
                D3D12_HEAP_FLAG_NONE,
                &uploadDescription,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(uploadBuffer.ReleaseAndGetAddressOf())),
            "Create texture upload buffer");

        // Copy tightly packed WIC RGBA rows into the D3D12-aligned upload
        // footprint without changing the source image dimensions.
        std::byte* mappedPixels = nullptr;
        D3D12_RANGE noReadRange{0, 0};
        ThrowIfFailed(
            uploadBuffer->Map(
                0,
                &noReadRange,
                reinterpret_cast<void**>(&mappedPixels)),
            "Map texture upload buffer");
        const std::size_t sourceStride =
            static_cast<std::size_t>(image.width) * 4;
        for (UINT row = 0; row < rowCount; ++row)
        {
            std::memcpy(
                mappedPixels + footprint.Offset +
                    static_cast<std::size_t>(row) *
                        footprint.Footprint.RowPitch,
                image.pixels.data() +
                    static_cast<std::size_t>(row) * sourceStride,
                sourceStride);
        }
        uploadBuffer->Unmap(0, nullptr);

        // Record the copy and transition so the resource is shader-readable
        // when the initialization upload fence completes.
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = uploadBuffer.Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint = footprint;
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = texture.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.SubresourceIndex = 0;
        uploadCommandList_->CopyTextureRegion(
            &destination,
            0,
            0,
            0,
            &source,
            nullptr);

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = texture.Get();
        barrier.Transition.Subresource =
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter =
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        uploadCommandList_->ResourceBarrier(1, &barrier);

        // The descriptor overwrites one null entry in this TextureSet's fixed
        // 64-slot SRV block; differently sized Texture2D resources stay
        // independent.
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Shader4ComponentMapping =
            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Texture2D.MipLevels = 1;
        device_->CreateShaderResourceView(
            texture.Get(),
            &view,
            OffsetCpuDescriptor(
                descriptorBlockStart,
                static_cast<std::uint32_t>(descriptorIndex),
                descriptorSize_));

        textureSet.textureInfo_.push_back(
            TextureInfo{image.width, image.height});
        textureSet.resources_.push_back(std::move(texture));
        uploadBuffers.push_back(std::move(uploadBuffer));
    }

    void TextureManager::ExecuteUploadAndWait()
    {
        ID3D12CommandList* commandLists[]{uploadCommandList_.Get()};
        commandQueue_->ExecuteCommandLists(1, commandLists);

        // Texture loading is an initialization path. Waiting here keeps the
        // temporary upload buffers local and adds no normal-frame GPU wait.
        const std::uint64_t fenceValue = nextUploadFenceValue_++;
        ThrowIfFailed(
            commandQueue_->Signal(uploadFence_.Get(), fenceValue),
            "Signal texture upload fence");
        if (uploadFence_->GetCompletedValue() >= fenceValue)
        {
            return;
        }

        ThrowIfFailed(
            uploadFence_->SetEventOnCompletion(
                fenceValue,
                uploadFenceEvent_),
            "Wait for texture upload fence");
        if (WaitForSingleObject(uploadFenceEvent_, INFINITE) != WAIT_OBJECT_0)
        {
            throw std::runtime_error(
                "Waiting for texture upload completion failed.");
        }
    }

    ID3D12DescriptorHeap* TextureManager::DescriptorHeap() const noexcept
    {
        return descriptorHeap_.Get();
    }

    D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GpuDescriptorStart(
        const TextureSetHandle& textureSet) const
    {
        if (!initialized_ || textureSet == nullptr)
        {
            throw std::invalid_argument(
                "A GPU descriptor table requires an initialized manager and texture set.");
        }
        return textureSet->gpuDescriptorStart_;
    }

    void TextureManager::BeginRenderTargetPass(
        ID3D12GraphicsCommandList& commandList,
        const RenderTargetTextureHandle& target,
        const DirectX::XMFLOAT4& clearColor)
    {
        if (!initialized_ || target == nullptr)
        {
            throw std::invalid_argument(
                "Beginning a texture pass requires an initialized manager and target.");
        }

        if (target->state_ != D3D12_RESOURCE_STATE_RENDER_TARGET)
        {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = target->resource_.Get();
            barrier.Transition.Subresource =
                D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = target->state_;
            barrier.Transition.StateAfter =
                D3D12_RESOURCE_STATE_RENDER_TARGET;
            commandList.ResourceBarrier(1, &barrier);
            target->state_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
        }

        const D3D12_VIEWPORT viewport{
            0.0F,
            0.0F,
            static_cast<float>(target->width_),
            static_cast<float>(target->height_),
            0.0F,
            1.0F};
        const D3D12_RECT scissor{
            0,
            0,
            static_cast<LONG>(target->width_),
            static_cast<LONG>(target->height_)};
        commandList.RSSetViewports(1, &viewport);
        commandList.RSSetScissorRects(1, &scissor);
        commandList.OMSetRenderTargets(1, &target->rtv_, FALSE, nullptr);
        const float color[]{
            clearColor.x,
            clearColor.y,
            clearColor.z,
            clearColor.w};
        commandList.ClearRenderTargetView(
            target->rtv_, color, 0, nullptr);
    }

    void TextureManager::EndRenderTargetPass(
        ID3D12GraphicsCommandList& commandList,
        const RenderTargetTextureHandle& target)
    {
        if (!initialized_ || target == nullptr)
        {
            throw std::invalid_argument(
                "Ending a texture pass requires an initialized manager and target.");
        }
        if (target->state_ == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
        {
            return;
        }

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = target->resource_.Get();
        barrier.Transition.Subresource =
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = target->state_;
        barrier.Transition.StateAfter =
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        commandList.ResourceBarrier(1, &barrier);
        target->state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
}
