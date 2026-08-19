#include "Visual2D/D3D12Visual2DRenderer.h"

#include "Primitive/RectangleShape.h"
#include "Shader/ShaderCompiler.h"
#include "Text/TextRendering.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace mrg::graphics
{
    namespace
    {
        constexpr std::uint32_t MaximumScreenCanvasZOrder = 31;
        constexpr float ScreenUiDepthRange = 0.10F;
        constexpr float ScreenVisual2DCanvasDepthBand =
            ScreenUiDepthRange /
            static_cast<float>(MaximumScreenCanvasZOrder + 1);

        void ThrowIfFailed(const HRESULT result, const char* operation)
        {
            if (FAILED(result))
            {
                throw std::runtime_error(
                    std::string(operation) + " failed with HRESULT " +
                    std::to_string(static_cast<long>(result)) + ".");
            }
        }

        [[nodiscard]] DirectX::XMFLOAT4 ToFloat4(
            const visual2d::Color color) noexcept
        {
            return {color.red, color.green, color.blue, color.alpha};
        }

        [[nodiscard]] TextHorizontalAlignment ToTextAlignment(
            const visual2d::TextAlignment alignment) noexcept
        {
            switch (alignment)
            {
            case visual2d::TextAlignment::Center:
                return TextHorizontalAlignment::Center;
            case visual2d::TextAlignment::Trailing:
                return TextHorizontalAlignment::Trailing;
            default:
                return TextHorizontalAlignment::Leading;
            }
        }

        [[nodiscard]] D3D12_RESOURCE_DESC BufferDescription(
            const std::size_t byteCount) noexcept
        {
            D3D12_RESOURCE_DESC result{};
            result.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            result.Width = byteCount;
            result.Height = 1;
            result.DepthOrArraySize = 1;
            result.MipLevels = 1;
            result.SampleDesc.Count = 1;
            result.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            return result;
        }
    }

    struct D3D12Visual2DRenderer::Impl
    {
        struct RectangleInstance
        {
            DirectX::XMFLOAT4 bounds{};
            DirectX::XMFLOAT4 color{};
            DirectX::XMFLOAT4X4 transform{};
        };

        struct VisualInstance
        {
            DirectX::XMFLOAT4 bounds{};
            DirectX::XMFLOAT4 color{};
            DirectX::XMFLOAT4X4 transform{};
            DirectX::XMFLOAT4 uvTransform{};
            std::uint32_t textureIndex{NoTextureIndex};
            std::uint32_t padding[3]{};
        };

        static_assert(sizeof(RectangleInstance) == 96);
        static_assert(sizeof(VisualInstance) == 128);

        struct FrameBufferPage
        {
            ComPtr<ID3D12Resource> resource;
            std::byte* mappedData{};
            std::size_t capacity{};
            std::size_t used{};
        };

        struct FrameUploadArena
        {
            std::vector<FrameBufferPage> pages;
            std::uint64_t renderIndex{};
        };

        struct ImageResource
        {
            std::uint32_t pageIndex{};
            std::uint32_t textureIndex{};
        };

        struct ImagePage
        {
            TextureSetHandle textures;
            MaterialInstanceHandle material;
        };

        struct FrameRenderTargetLifetime
        {
            std::vector<RenderTargetTextureHandle> targets;
            std::uint64_t renderIndex{};
        };

        ~Impl()
        {
            Shutdown();
        }

        void Initialize(
            MeshRenderSystem& meshes,
            TextRenderSystem& text)
        {
            if (initialized)
            {
                throw std::logic_error(
                    "The D3D12 Visual2D renderer is already initialized.");
            }
            if (meshes.Device() == nullptr)
            {
                throw std::logic_error(
                    "The mesh renderer has no D3D12 device.");
            }

            meshRendering = &meshes;
            screenTextRendering = &text;
            device = meshes.Device();

            // Visual2D world matrices scale this mesh directly to the requested
            // pixel/logical Bounds. RectangleShape defaults to 2x2, so use an
            // explicit 1x1 unit quad to keep rendering and hit-test sizes equal.
            const geometry::RectangleShape rectangle(1.0F, 1.0F);
            rectangleMesh = meshes.CreateMesh<
                geometry::VertexPositionColor>(rectangle);
            imageMesh = meshes.CreateMesh<
                geometry::VertexPositionUvColor>(rectangle);
            rectangleMaterial = meshes.CreateMaterial(
                BuiltInMaterial::UnlitVertexColor);
            screenFont = text.LoadSystemFont(L"Segoe UI");

            textureTextRendering.Initialize(
                *device,
                DXGI_FORMAT_R8G8B8A8_UNORM);
            textureFont = textureTextRendering.LoadSystemFont(L"Segoe UI");
            CreateRectanglePipeline();
            CreateVisualPipeline();
            initialized = true;
        }

        void Shutdown() noexcept
        {
            for (FrameUploadArena& arena : rectangleArenas)
            {
                for (FrameBufferPage& page : arena.pages)
                {
                    if (page.resource != nullptr && page.mappedData != nullptr)
                    {
                        page.resource->Unmap(0, nullptr);
                    }
                }
                arena = {};
            }
            for (FrameUploadArena& arena : visualArenas)
            {
                for (FrameBufferPage& page : arena.pages)
                {
                    if (page.resource != nullptr && page.mappedData != nullptr)
                    {
                        page.resource->Unmap(0, nullptr);
                    }
                }
                arena = {};
            }
            rectanglePipeline.Reset();
            rectangleRootSignature.Reset();
            visualPipeline.Reset();
            visualRootSignature.Reset();
            textureFont.reset();
            textureTextRendering.Shutdown();
            screenFont.reset();
            rectangleMaterial.reset();
            imagesByHandle.clear();
            imageHandlesByPath.clear();
            imagePages.clear();
            renderTargetsInFlight = {};
            nextImageHandle = 1;
            imageMesh.reset();
            rectangleMesh.reset();
            screenTextRendering = nullptr;
            meshRendering = nullptr;
            device = nullptr;
            initialized = false;
        }

        [[nodiscard]] DirectX::XMMATRIX QuadToLocal(
            const visual2d::Rect bounds) noexcept
        {
            return DirectX::XMMatrixScaling(
                    bounds.width,
                    bounds.height,
                    1.0F) *
                DirectX::XMMatrixTranslation(
                    bounds.x + bounds.width * 0.5F,
                    bounds.y + bounds.height * 0.5F,
                    0.0F);
        }

        [[nodiscard]] visual2d::Rect TransformBounds(
            const visual2d::Rect bounds,
            const DirectX::XMFLOAT4X4& transform) noexcept
        {
            const DirectX::XMMATRIX matrix =
                DirectX::XMLoadFloat4x4(&transform);
            const DirectX::XMVECTOR corners[]{
                DirectX::XMVectorSet(bounds.x, bounds.y, 0.0F, 1.0F),
                DirectX::XMVectorSet(
                    bounds.x + bounds.width, bounds.y, 0.0F, 1.0F),
                DirectX::XMVectorSet(
                    bounds.x, bounds.y + bounds.height, 0.0F, 1.0F),
                DirectX::XMVectorSet(
                    bounds.x + bounds.width,
                    bounds.y + bounds.height,
                    0.0F,
                    1.0F)};
            float minimumX = std::numeric_limits<float>::max();
            float minimumY = std::numeric_limits<float>::max();
            float maximumX = std::numeric_limits<float>::lowest();
            float maximumY = std::numeric_limits<float>::lowest();
            for (const DirectX::XMVECTOR corner : corners)
            {
                const DirectX::XMVECTOR transformed =
                    DirectX::XMVector3TransformCoord(corner, matrix);
                minimumX = std::min(minimumX, DirectX::XMVectorGetX(transformed));
                minimumY = std::min(minimumY, DirectX::XMVectorGetY(transformed));
                maximumX = std::max(maximumX, DirectX::XMVectorGetX(transformed));
                maximumY = std::max(maximumY, DirectX::XMVectorGetY(transformed));
            }
            return {minimumX, minimumY, maximumX - minimumX, maximumY - minimumY};
        }

        [[nodiscard]] visual2d::ImageHandle LoadImage(
            const std::filesystem::path& path)
        {
            if (!initialized || meshRendering == nullptr || path.empty())
            {
                throw std::invalid_argument(
                    "Loading a Visual2D image requires an initialized renderer and path.");
            }

            const std::filesystem::path normalized = path.lexically_normal();
            const std::wstring cacheKey = normalized.wstring();
            if (const auto existing = imageHandlesByPath.find(cacheKey);
                existing != imageHandlesByPath.end())
            {
                return visual2d::ImageHandle{existing->second};
            }

            const bool requiresNewPage = imagePages.empty() ||
                imagePages.back().textures->Size() >= MaxTexturesPerSet;
            if (requiresNewPage)
            {
                const std::array paths{normalized};
                ImagePage page{};
                page.textures = meshRendering->Textures().LoadTextureSet(paths);
                page.material = meshRendering->CreateMaterial(
                    BuiltInMaterial::
                        UnlitVertexColorTextureArrayAlphaBlend);
                page.material->SetTextureSet(page.textures);
                imagePages.push_back(std::move(page));
            }
            else
            {
                ImagePage& page = imagePages.back();
                page.textures = meshRendering->Textures().AppendTexture(
                    page.textures,
                    normalized);
            }

            const std::uint64_t handleValue = nextImageHandle++;
            const std::uint32_t pageIndex = static_cast<std::uint32_t>(
                imagePages.size() - 1);
            imageHandlesByPath.emplace(cacheKey, handleValue);
            imagesByHandle.emplace(
                handleValue,
                ImageResource{
                    pageIndex,
                    static_cast<std::uint32_t>(
                        imagePages.back().textures->Size() - 1)});
            return visual2d::ImageHandle{handleValue};
        }

        [[nodiscard]] visual2d::Size GetImageSize(
            const visual2d::ImageHandle image) const noexcept
        {
            const auto found = imagesByHandle.find(image.value);
            if (!image || found == imagesByHandle.end())
            {
                return {};
            }

            const ImageResource& resource = found->second;
            const TextureInfo& info = imagePages[resource.pageIndex].textures->
                Info(resource.textureIndex);
            return {
                static_cast<float>(info.width),
                static_cast<float>(info.height)};
        }

        void CreateRectanglePipeline()
        {
            std::array<D3D12_ROOT_PARAMETER, 2> parameters{};
            parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
            parameters[0].Descriptor.ShaderRegister = 0;
            parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
            parameters[1].ParameterType =
                D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            parameters[1].Constants.ShaderRegister = 0;
            parameters[1].Constants.Num32BitValues = 2;
            parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

            D3D12_ROOT_SIGNATURE_DESC root{};
            root.NumParameters = static_cast<UINT>(parameters.size());
            root.pParameters = parameters.data();
            root.Flags =
                D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

            ComPtr<ID3DBlob> serialized;
            ComPtr<ID3DBlob> errors;
            const HRESULT serializationResult = D3D12SerializeRootSignature(
                &root,
                D3D_ROOT_SIGNATURE_VERSION_1,
                serialized.ReleaseAndGetAddressOf(),
                errors.ReleaseAndGetAddressOf());
            if (FAILED(serializationResult))
            {
                const std::string details = errors != nullptr
                    ? std::string(
                        static_cast<const char*>(errors->GetBufferPointer()),
                        errors->GetBufferSize())
                    : "No root-signature diagnostics were returned.";
                throw std::runtime_error(
                    "UI rectangle root signature failed: " + details);
            }
            ThrowIfFailed(
                device->CreateRootSignature(
                    0,
                    serialized->GetBufferPointer(),
                    serialized->GetBufferSize(),
                    IID_PPV_ARGS(
                        rectangleRootSignature.ReleaseAndGetAddressOf())),
                "Create UI rectangle root signature");

            const ComPtr<ID3DBlob> vertexShader = detail::CompileShader(
                detail::BuiltInShader::Visual2DRectangle,
                "VSMain",
                "vs_5_1");
            const ComPtr<ID3DBlob> pixelShader = detail::CompileShader(
                detail::BuiltInShader::Visual2DRectangle,
                "PSMain",
                "ps_5_1");
            D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
            pipeline.pRootSignature = rectangleRootSignature.Get();
            pipeline.VS = {
                vertexShader->GetBufferPointer(),
                vertexShader->GetBufferSize()};
            pipeline.PS = {
                pixelShader->GetBufferPointer(),
                pixelShader->GetBufferSize()};
            D3D12_RENDER_TARGET_BLEND_DESC& blend =
                pipeline.BlendState.RenderTarget[0];
            blend.BlendEnable = TRUE;
            blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
            blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            blend.BlendOp = D3D12_BLEND_OP_ADD;
            blend.SrcBlendAlpha = D3D12_BLEND_ONE;
            blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
            blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            pipeline.SampleMask = UINT_MAX;
            pipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            pipeline.RasterizerState.DepthClipEnable = TRUE;
            pipeline.DepthStencilState.DepthEnable = FALSE;
            pipeline.DepthStencilState.StencilEnable = FALSE;
            pipeline.PrimitiveTopologyType =
                D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            pipeline.NumRenderTargets = 1;
            pipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            pipeline.SampleDesc.Count = 1;
            ThrowIfFailed(
                device->CreateGraphicsPipelineState(
                    &pipeline,
                    IID_PPV_ARGS(rectanglePipeline.ReleaseAndGetAddressOf())),
                "Create UI rectangle pipeline");
        }

        void CreateVisualPipeline()
        {
            D3D12_DESCRIPTOR_RANGE textureRange{};
            textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            textureRange.NumDescriptors = MaxTexturesPerSet;
            textureRange.BaseShaderRegister = 0;
            textureRange.OffsetInDescriptorsFromTableStart = 0;

            std::array<D3D12_ROOT_PARAMETER, 3> parameters{};
            parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
            parameters[0].Descriptor.ShaderRegister = 0;
            parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
            parameters[1].ParameterType =
                D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            parameters[1].Constants.ShaderRegister = 0;
            parameters[1].Constants.Num32BitValues = 2;
            parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
            parameters[2].ParameterType =
                D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameters[2].DescriptorTable.NumDescriptorRanges = 1;
            parameters[2].DescriptorTable.pDescriptorRanges = &textureRange;
            parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

            D3D12_STATIC_SAMPLER_DESC sampler{};
            sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            sampler.BorderColor =
                D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
            sampler.MinLOD = 0.0F;
            sampler.MaxLOD = D3D12_FLOAT32_MAX;
            sampler.ShaderRegister = 0;
            sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

            D3D12_ROOT_SIGNATURE_DESC root{};
            root.NumParameters = static_cast<UINT>(parameters.size());
            root.pParameters = parameters.data();
            root.NumStaticSamplers = 1;
            root.pStaticSamplers = &sampler;
            root.Flags =
                D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

            ComPtr<ID3DBlob> serialized;
            ComPtr<ID3DBlob> errors;
            const HRESULT serializationResult = D3D12SerializeRootSignature(
                &root,
                D3D_ROOT_SIGNATURE_VERSION_1,
                serialized.ReleaseAndGetAddressOf(),
                errors.ReleaseAndGetAddressOf());
            if (FAILED(serializationResult))
            {
                const std::string details = errors != nullptr
                    ? std::string(
                        static_cast<const char*>(errors->GetBufferPointer()),
                        errors->GetBufferSize())
                    : "No root-signature diagnostics were returned.";
                throw std::runtime_error(
                    "Visual2D image root signature failed: " + details);
            }
            ThrowIfFailed(
                device->CreateRootSignature(
                    0,
                    serialized->GetBufferPointer(),
                    serialized->GetBufferSize(),
                    IID_PPV_ARGS(visualRootSignature.ReleaseAndGetAddressOf())),
                "Create Visual2D image root signature");

            const ComPtr<ID3DBlob> vertexShader = detail::CompileShader(
                detail::BuiltInShader::Visual2DRectangle,
                "ImageVSMain",
                "vs_5_1");
            const ComPtr<ID3DBlob> pixelShader = detail::CompileShader(
                detail::BuiltInShader::Visual2DRectangle,
                "ImagePSMain",
                "ps_5_1");
            D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
            pipeline.pRootSignature = visualRootSignature.Get();
            pipeline.VS = {
                vertexShader->GetBufferPointer(), vertexShader->GetBufferSize()};
            pipeline.PS = {
                pixelShader->GetBufferPointer(), pixelShader->GetBufferSize()};
            D3D12_RENDER_TARGET_BLEND_DESC& blend =
                pipeline.BlendState.RenderTarget[0];
            blend.BlendEnable = TRUE;
            blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
            blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            blend.BlendOp = D3D12_BLEND_OP_ADD;
            blend.SrcBlendAlpha = D3D12_BLEND_ONE;
            blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
            blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            pipeline.SampleMask = UINT_MAX;
            pipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            pipeline.RasterizerState.DepthClipEnable = TRUE;
            pipeline.DepthStencilState.DepthEnable = FALSE;
            pipeline.DepthStencilState.StencilEnable = FALSE;
            pipeline.PrimitiveTopologyType =
                D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            pipeline.NumRenderTargets = 1;
            pipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            pipeline.SampleDesc.Count = 1;
            ThrowIfFailed(
                device->CreateGraphicsPipelineState(
                    &pipeline,
                    IID_PPV_ARGS(visualPipeline.ReleaseAndGetAddressOf())),
                "Create Visual2D image pipeline");
        }

        [[nodiscard]] FrameBufferPage& AllocateUploadRange(
            FrameUploadArena& arena,
            const std::uint64_t renderIndex,
            const std::size_t required,
            const std::size_t instanceSize,
            const char* operation,
            std::size_t& firstInstance)
        {
            if (arena.renderIndex != renderIndex)
            {
                arena.renderIndex = renderIndex;
                for (FrameBufferPage& page : arena.pages)
                {
                    page.used = 0;
                }
            }

            for (FrameBufferPage& page : arena.pages)
            {
                if (page.capacity - page.used >= required)
                {
                    firstInstance = page.used;
                    page.used += required;
                    return page;
                }
            }

            std::size_t capacity = 64;
            while (capacity < required)
            {
                capacity *= 2;
            }
            D3D12_HEAP_PROPERTIES uploadHeap{};
            uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
            const D3D12_RESOURCE_DESC description = BufferDescription(
                capacity * instanceSize);
            FrameBufferPage page{};
            ThrowIfFailed(
                device->CreateCommittedResource(
                    &uploadHeap,
                    D3D12_HEAP_FLAG_NONE,
                    &description,
                    D3D12_RESOURCE_STATE_GENERIC_READ,
                    nullptr,
                    IID_PPV_ARGS(page.resource.ReleaseAndGetAddressOf())),
                operation);
            ThrowIfFailed(
                page.resource->Map(
                    0,
                    nullptr,
                    reinterpret_cast<void**>(&page.mappedData)),
                "Map Visual2D upload arena page");
            page.capacity = capacity;
            page.used = required;
            firstInstance = 0;
            arena.pages.push_back(std::move(page));
            return arena.pages.back();
        }

        void DrawRectangles(
            ID3D12GraphicsCommandList& commandList,
            const std::uint32_t frameIndex,
            const std::uint64_t renderIndex,
            const std::uint32_t width,
            const std::uint32_t height,
            const std::vector<RectangleInstance>& rectangles)
        {
            if (rectangles.empty())
            {
                return;
            }
            std::size_t firstInstance = 0;
            FrameBufferPage& page = AllocateUploadRange(
                rectangleArenas[frameIndex],
                renderIndex,
                rectangles.size(),
                sizeof(RectangleInstance),
                "Create Visual2D rectangle upload arena page",
                firstInstance);
            std::memcpy(
                page.mappedData + firstInstance * sizeof(RectangleInstance),
                rectangles.data(),
                rectangles.size() * sizeof(RectangleInstance));

            commandList.SetGraphicsRootSignature(
                rectangleRootSignature.Get());
            commandList.SetPipelineState(rectanglePipeline.Get());
            commandList.IASetPrimitiveTopology(
                D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            commandList.SetGraphicsRootShaderResourceView(
                0,
                page.resource->GetGPUVirtualAddress() +
                    firstInstance * sizeof(RectangleInstance));
            const std::array constants{
                std::bit_cast<std::uint32_t>(static_cast<float>(width)),
                std::bit_cast<std::uint32_t>(static_cast<float>(height))};
            commandList.SetGraphicsRoot32BitConstants(
                1,
                static_cast<UINT>(constants.size()),
                constants.data(),
                0);
            commandList.DrawInstanced(
                6,
                static_cast<UINT>(rectangles.size()),
                0,
                0);
        }

        void DrawVisuals(
            ID3D12GraphicsCommandList& commandList,
            const std::uint32_t frameIndex,
            const std::uint64_t renderIndex,
            const std::uint32_t width,
            const std::uint32_t height,
            const TextureSetHandle& textures,
            const std::vector<VisualInstance>& visuals)
        {
            if (visuals.empty() || textures == nullptr)
            {
                return;
            }
            std::size_t firstInstance = 0;
            FrameBufferPage& page = AllocateUploadRange(
                visualArenas[frameIndex],
                renderIndex,
                visuals.size(),
                sizeof(VisualInstance),
                "Create Visual2D image upload arena page",
                firstInstance);
            std::memcpy(
                page.mappedData + firstInstance * sizeof(VisualInstance),
                visuals.data(),
                visuals.size() * sizeof(VisualInstance));

            ID3D12DescriptorHeap* descriptorHeaps[]{
                meshRendering->Textures().DescriptorHeap()};
            commandList.SetDescriptorHeaps(1, descriptorHeaps);
            commandList.SetGraphicsRootSignature(visualRootSignature.Get());
            commandList.SetPipelineState(visualPipeline.Get());
            commandList.IASetPrimitiveTopology(
                D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            commandList.SetGraphicsRootShaderResourceView(
                0,
                page.resource->GetGPUVirtualAddress() +
                    firstInstance * sizeof(VisualInstance));
            const std::array constants{
                std::bit_cast<std::uint32_t>(static_cast<float>(width)),
                std::bit_cast<std::uint32_t>(static_cast<float>(height))};
            commandList.SetGraphicsRoot32BitConstants(
                1,
                static_cast<UINT>(constants.size()),
                constants.data(),
                0);
            commandList.SetGraphicsRootDescriptorTable(
                2,
                meshRendering->Textures().GpuDescriptorStart(textures));
            commandList.DrawInstanced(
                6,
                static_cast<UINT>(visuals.size()),
                0,
                0);
        }

        MeshRenderSystem* meshRendering{};
        TextRenderSystem* screenTextRendering{};
        ID3D12Device* device{};
        GpuMeshHandle rectangleMesh;
        GpuMeshHandle imageMesh;
        MaterialInstanceHandle rectangleMaterial;
        std::unordered_map<std::uint64_t, ImageResource> imagesByHandle;
        std::unordered_map<std::wstring, std::uint64_t> imageHandlesByPath;
        std::vector<ImagePage> imagePages;
        std::uint64_t nextImageHandle{1};
        FontHandle screenFont;
        TextRenderSystem textureTextRendering;
        FontHandle textureFont;
        ComPtr<ID3D12RootSignature> rectangleRootSignature;
        ComPtr<ID3D12PipelineState> rectanglePipeline;
        std::array<FrameUploadArena, D3D12Renderer::FrameCount>
            rectangleArenas{};
        ComPtr<ID3D12RootSignature> visualRootSignature;
        ComPtr<ID3D12PipelineState> visualPipeline;
        std::array<FrameUploadArena, D3D12Renderer::FrameCount> visualArenas{};
        std::array<FrameRenderTargetLifetime, D3D12Renderer::FrameCount>
            renderTargetsInFlight{};
        bool initialized{};
    };

    D3D12Visual2DRenderer::D3D12Visual2DRenderer()
        : implementation_(std::make_unique<Impl>())
    {
    }

    D3D12Visual2DRenderer::~D3D12Visual2DRenderer() = default;

    void D3D12Visual2DRenderer::Initialize(
        MeshRenderSystem& meshRendering,
        TextRenderSystem& textRendering)
    {
        implementation_->Initialize(meshRendering, textRendering);
    }

    void D3D12Visual2DRenderer::Shutdown() noexcept
    {
        implementation_->Shutdown();
    }

    visual2d::ImageHandle D3D12Visual2DRenderer::LoadImage(
        const std::filesystem::path& path)
    {
        return implementation_->LoadImage(path);
    }

    visual2d::Size D3D12Visual2DRenderer::GetImageSize(
        const visual2d::ImageHandle image) const noexcept
    {
        return implementation_->GetImageSize(image);
    }

    void D3D12Visual2DRenderer::SubmitScreen(
        const visual2d::Visual2DCanvas& canvas,
        const RenderContext& context,
        const visual2d::Point screenOrigin,
        const std::uint32_t canvasZOrder)
    {
        Impl& state = *implementation_;
        if (!IsInitialized() || context.meshRendering != state.meshRendering ||
            context.textRendering != state.screenTextRendering)
        {
            throw std::logic_error(
                "The D3D12 Visual2D renderer is not initialized for this context.");
        }

        const std::vector<visual2d::DrawPacket> commands = canvas.BuildDrawList();
        DirectX::XMFLOAT4X4 viewProjection{};
        DirectX::XMStoreFloat4x4(
            &viewProjection,
            DirectX::XMMatrixOrthographicOffCenterLH(
                0.0F,
                static_cast<float>(context.width),
                static_cast<float>(context.height),
                0.0F,
                0.0F,
                1.0F));

        const float pixelScale = canvas.PixelScale();

        const float canvasFarDepth = ScreenUiDepthRange -
            static_cast<float>(std::min(
                canvasZOrder,
                MaximumScreenCanvasZOrder)) * ScreenVisual2DCanvasDepthBand;
        for (std::size_t index = 0; index < commands.size(); ++index)
        {
            const visual2d::DrawPacket& command = commands[index];
            // Visual2DCanvas already returns the tree in paint order. Keep every
            // Canvas inside its own depth band so opening a popup can change
            // its local command count without crossing another Canvas.
            const float normalizedOrder = commands.empty()
                ? 0.0F
                : static_cast<float>(index + 1) /
                    static_cast<float>(commands.size() + 1);
            const float depth = canvasFarDepth -
                ScreenVisual2DCanvasDepthBand * normalizedOrder;
            const visual2d::Rect logicalBounds = state.TransformBounds(
                command.bounds,
                command.nodeTransform);
            const visual2d::Rect bounds{
                screenOrigin.x + logicalBounds.x * pixelScale,
                screenOrigin.y + logicalBounds.y * pixelScale,
                logicalBounds.width * pixelScale,
                logicalBounds.height * pixelScale};
            if (command.type == visual2d::DrawPacketType::Text)
            {
                TextDrawCommand text{};
                text.positionPixels = {
                    command.bounds.x * pixelScale,
                    command.bounds.y * pixelScale};
                text.layoutSizePixels = {
                    command.bounds.width * pixelScale,
                    command.bounds.height * pixelScale};
                text.depth = depth;
                DirectX::XMStoreFloat4x4(
                    &text.transform,
                    DirectX::XMMatrixScaling(
                        1.0F / pixelScale,
                        1.0F / pixelScale,
                        1.0F) *
                    DirectX::XMLoadFloat4x4(&command.nodeTransform) *
                    DirectX::XMMatrixScaling(
                        pixelScale,
                        pixelScale,
                        0.0001F) *
                    DirectX::XMMatrixTranslation(
                        screenOrigin.x,
                        screenOrigin.y,
                        0.0F));
                text.horizontalAlignment = ToTextAlignment(
                    command.horizontalAlignment);
                text.verticalAlignment = TextVerticalAlignment::Center;
                text.style.font = state.screenFont;
                text.style.fontSizePixels = command.fontSize * pixelScale;
                text.style.color = ToFloat4(command.color);
                state.screenTextRendering->Submit(command.text, text);
                continue;
            }
            if (command.type == visual2d::DrawPacketType::Image)
            {
                const auto image = state.imagesByHandle.find(
                    command.image.value);
                if (image == state.imagesByHandle.end() ||
                    bounds.width <= 0.0F || bounds.height <= 0.0F)
                {
                    continue;
                }

                DirectX::XMFLOAT4X4 world{};
                DirectX::XMStoreFloat4x4(
                    &world,
                    state.QuadToLocal(command.bounds) *
                    DirectX::XMLoadFloat4x4(&command.nodeTransform) *
                    DirectX::XMMatrixScaling(
                        pixelScale,
                        pixelScale,
                        0.0001F) *
                    DirectX::XMMatrixTranslation(
                        screenOrigin.x,
                        screenOrigin.y,
                        depth));

                // RectangleShape's mesh-space positive Y has V=0, while a
                // screen Canvas uses positive Y downward. The geometry is
                // therefore vertically reversed only on the screen path.
                // Compose that correction with the caller's UV transform so
                // image cropping and intentional negative UV scales remain
                // correct instead of special-casing PNG assets in a Client.
                const DirectX::XMFLOAT2 screenUvScale{
                    command.uvScale.x,
                    -command.uvScale.y};
                const DirectX::XMFLOAT2 screenUvOffset{
                    command.uvOffset.x,
                    command.uvOffset.y + command.uvScale.y};
                state.meshRendering->Submit(
                    state.imageMesh,
                    state.imagePages[image->second.pageIndex].material,
                    world,
                    viewProjection,
                    ToFloat4(command.color),
                    screenUvScale,
                    screenUvOffset,
                    image->second.textureIndex);
                continue;
            }
            if (bounds.width <= 0.0F || bounds.height <= 0.0F)
            {
                continue;
            }

            DirectX::XMFLOAT4X4 world{};
            DirectX::XMStoreFloat4x4(
                &world,
                state.QuadToLocal(command.bounds) *
                DirectX::XMLoadFloat4x4(&command.nodeTransform) *
                DirectX::XMMatrixScaling(
                    pixelScale,
                    pixelScale,
                    0.0001F) *
                DirectX::XMMatrixTranslation(
                    screenOrigin.x,
                    screenOrigin.y,
                    depth));
            state.meshRendering->Submit(
                state.rectangleMesh,
                state.rectangleMaterial,
                world,
                viewProjection,
                ToFloat4(command.color),
                {1.0F, 1.0F},
                {0.0F, 0.0F},
                NoTextureIndex);
        }
    }

    void D3D12Visual2DRenderer::SubmitPlane(
        const visual2d::Visual2DCanvas& canvas,
        const RenderContext& context,
        const DirectX::XMFLOAT4X4& surfaceWorld,
        const visual2d::Size surfaceWorldSize,
        const DirectX::XMFLOAT4X4& viewProjection)
    {
        Impl& state = *implementation_;
        if (!IsInitialized() || context.meshRendering != state.meshRendering)
        {
            throw std::logic_error(
                "The D3D12 Visual2D renderer is not initialized for this context.");
        }
        if (surfaceWorldSize.width <= 0.0F || surfaceWorldSize.height <= 0.0F)
        {
            throw std::invalid_argument(
                "A world UI plane must have positive size.");
        }

        const visual2d::Size canvasSize = canvas.LogicalSize();
        const std::vector<visual2d::DrawPacket> commands = canvas.BuildDrawList();
        const DirectX::XMMATRIX surface =
            DirectX::XMLoadFloat4x4(&surfaceWorld);
        for (std::size_t index = 0; index < commands.size(); ++index)
        {
            const visual2d::DrawPacket& command = commands[index];
            if (command.bounds.width <= 0.0F || command.bounds.height <= 0.0F)
            {
                continue;
            }

            const float layer = -static_cast<float>(index) * 0.0005F;
            DirectX::XMFLOAT4X4 world{};
            DirectX::XMStoreFloat4x4(
                &world,
                state.QuadToLocal(command.bounds) *
                DirectX::XMLoadFloat4x4(&command.nodeTransform) *
                DirectX::XMMatrixScaling(
                    surfaceWorldSize.width / canvasSize.width,
                    -surfaceWorldSize.height / canvasSize.height,
                    1.0F) *
                DirectX::XMMatrixTranslation(
                    -surfaceWorldSize.width * 0.5F,
                    surfaceWorldSize.height * 0.5F,
                    layer) *
                surface);

            if (command.type == visual2d::DrawPacketType::Image)
            {
                const auto image = state.imagesByHandle.find(
                    command.image.value);
                if (image == state.imagesByHandle.end())
                {
                    continue;
                }

                state.meshRendering->Submit(
                    state.imageMesh,
                    state.imagePages[image->second.pageIndex].material,
                    world,
                    viewProjection,
                    ToFloat4(command.color),
                    command.uvScale,
                    command.uvOffset,
                    image->second.textureIndex);
                continue;
            }

            if (command.type != visual2d::DrawPacketType::Rectangle)
            {
                // Plane text is intentionally omitted. Curved surfaces use the
                // render-to-texture path below so glyphs can follow the mesh UVs.
                continue;
            }

            state.meshRendering->Submit(
                state.rectangleMesh,
                state.rectangleMaterial,
                world,
                viewProjection,
                ToFloat4(command.color),
                {1.0F, 1.0F},
                {0.0F, 0.0F},
                NoTextureIndex);
        }
    }

    RenderTargetTextureHandle D3D12Visual2DRenderer::CreateCanvasRenderTarget(
        const std::uint32_t width,
        const std::uint32_t height)
    {
        if (!IsInitialized())
        {
            throw std::logic_error(
                "The D3D12 Visual2D renderer must be initialized first.");
        }
        return implementation_->meshRendering->Textures().
            CreateRenderTargetTexture(width, height);
    }

    void D3D12Visual2DRenderer::RenderToTexture(
        const visual2d::Visual2DCanvas& canvas,
        const RenderTargetTextureHandle& target,
        const RenderContext& context)
    {
        Impl& state = *implementation_;
        if (!IsInitialized() || target == nullptr ||
            context.commandList == nullptr ||
            context.frameIndex >= D3D12Renderer::FrameCount)
        {
            throw std::invalid_argument(
                "The Canvas texture pass received an invalid context.");
        }

        // A transient Scene may release its target immediately after this
        // frame is recorded. Retain it until this frame-ring slot is reused,
        // which happens only after D3D12Renderer has waited for its fence.
        Impl::FrameRenderTargetLifetime& retainedTargets =
            state.renderTargetsInFlight[context.frameIndex];
        if (retainedTargets.renderIndex != context.renderIndex)
        {
            retainedTargets.renderIndex = context.renderIndex;
            retainedTargets.targets.clear();
        }
        retainedTargets.targets.push_back(target);

        ID3D12GraphicsCommandList& commandList = *context.commandList;
        TextureManager& textureManager = state.meshRendering->Textures();
        textureManager.BeginRenderTargetPass(
            commandList,
            target,
            {0.0F, 0.0F, 0.0F, 0.0F});
        const std::uint32_t targetWidth = target->Width();
        const std::uint32_t targetHeight = target->Height();

        const visual2d::Size canvasSize = canvas.LogicalSize();
        const float scaleX =
            static_cast<float>(targetWidth) / canvasSize.width;
        const float scaleY =
            static_cast<float>(targetHeight) / canvasSize.height;
        const std::vector<visual2d::DrawPacket> commands = canvas.BuildDrawList();
        std::vector<Impl::RectangleInstance> rectangles;
        rectangles.reserve(commands.size());
        std::vector<Impl::VisualInstance> visuals;
        visuals.reserve(commands.size());

        struct PendingText
        {
            std::wstring text;
            TextDrawCommand command;
        };
        std::vector<PendingText> textCommands;
        textCommands.reserve(commands.size());

        enum class BatchType
        {
            None,
            Rectangles,
            Visuals,
            Text,
        };
        BatchType batchType = BatchType::None;
        std::uint32_t batchImagePage = 0;

        // Preserve the Canvas tree's paint order. Consecutive primitives that
        // use the same pipeline and descriptor page remain instanced, while a
        // pipeline/page change closes the current batch before recording the
        // next one.
        const auto flushBatch = [&]()
        {
            switch (batchType)
            {
            case BatchType::Rectangles:
                state.DrawRectangles(
                    commandList,
                    context.frameIndex,
                    context.renderIndex,
                    targetWidth,
                    targetHeight,
                    rectangles);
                rectangles.clear();
                break;

            case BatchType::Visuals:
                state.DrawVisuals(
                    commandList,
                    context.frameIndex,
                    context.renderIndex,
                    targetWidth,
                    targetHeight,
                    state.imagePages[batchImagePage].textures,
                    visuals);
                visuals.clear();
                break;

            case BatchType::Text:
                state.textureTextRendering.BeginFrame(
                    context.frameIndex,
                    context.renderIndex);
                for (const PendingText& text : textCommands)
                {
                    state.textureTextRendering.Submit(
                        text.text,
                        text.command);
                }
                state.textureTextRendering.Flush(
                    commandList,
                    targetWidth,
                    targetHeight);
                textCommands.clear();
                break;

            case BatchType::None:
                break;
            }
            batchType = BatchType::None;
        };

        const auto selectBatch = [&flushBatch, &batchType, &batchImagePage](
            const BatchType requestedType,
            const std::uint32_t requestedImagePage = 0)
        {
            if (batchType != requestedType ||
                (requestedType == BatchType::Visuals &&
                 batchImagePage != requestedImagePage))
            {
                flushBatch();
                batchType = requestedType;
                batchImagePage = requestedImagePage;
            }
        };

        for (const visual2d::DrawPacket& command : commands)
        {
            if (command.bounds.width <= 0.0F ||
                command.bounds.height <= 0.0F)
            {
                continue;
            }

            DirectX::XMFLOAT4X4 primitiveTransform{};
            DirectX::XMStoreFloat4x4(
                &primitiveTransform,
                DirectX::XMLoadFloat4x4(&command.nodeTransform) *
                DirectX::XMMatrixScaling(scaleX, scaleY, 0.0001F));
            if (command.type == visual2d::DrawPacketType::Rectangle)
            {
                selectBatch(BatchType::Rectangles);
                rectangles.push_back({
                    {command.bounds.x,
                     command.bounds.y,
                     command.bounds.width,
                     command.bounds.height},
                    ToFloat4(command.color),
                    primitiveTransform});
                continue;
            }

            if (command.type == visual2d::DrawPacketType::Image)
            {
                const auto image = state.imagesByHandle.find(
                    command.image.value);
                if (image == state.imagesByHandle.end())
                {
                    continue;
                }
                selectBatch(
                    BatchType::Visuals,
                    image->second.pageIndex);
                visuals.push_back({
                    {command.bounds.x,
                     command.bounds.y,
                     command.bounds.width,
                     command.bounds.height},
                    ToFloat4(command.color),
                    primitiveTransform,
                    {command.uvScale.x,
                     command.uvScale.y,
                     command.uvOffset.x,
                     command.uvOffset.y},
                    image->second.textureIndex});
                continue;
            }

            TextDrawCommand text{};
            text.positionPixels = {
                command.bounds.x * scaleX,
                command.bounds.y * scaleY};
            text.layoutSizePixels = {
                command.bounds.width * scaleX,
                command.bounds.height * scaleY};
            DirectX::XMStoreFloat4x4(
                &text.transform,
                DirectX::XMMatrixScaling(
                    1.0F / scaleX,
                    1.0F / scaleY,
                    1.0F) *
                DirectX::XMLoadFloat4x4(&command.nodeTransform) *
                DirectX::XMMatrixScaling(scaleX, scaleY, 0.0001F));
            text.horizontalAlignment = ToTextAlignment(
                command.horizontalAlignment);
            text.verticalAlignment = TextVerticalAlignment::Center;
            text.style.font = state.textureFont;
            text.style.fontSizePixels = command.fontSize *
                std::min(scaleX, scaleY);
            text.style.color = ToFloat4(command.color);
            selectBatch(BatchType::Text);
            textCommands.push_back({command.text, std::move(text)});
        }
        flushBatch();

        textureManager.EndRenderTargetPass(commandList, target);

        commandList.RSSetViewports(1, &context.viewport);
        commandList.RSSetScissorRects(1, &context.scissorRectangle);
        commandList.OMSetRenderTargets(
            1,
            &context.renderTargetView,
            FALSE,
            &context.depthStencilView);
    }

    bool D3D12Visual2DRenderer::IsInitialized() const noexcept
    {
        return implementation_ != nullptr && implementation_->initialized;
    }
}
