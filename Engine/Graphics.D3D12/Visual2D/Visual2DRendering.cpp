#include "Visual2D/Visual2DRendering.h"

#include "Primitive/RectangleShape.h"
#include "Shader/ShaderCompiler.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <filesystem>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
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

        struct FrameBuffer
        {
            ComPtr<ID3D12Resource> resource;
            std::byte* mappedData{};
            std::size_t capacity{};
        };

        struct ImageResource
        {
            std::uint32_t textureIndex{};
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
            for (FrameBuffer& frame : rectangleBuffers)
            {
                if (frame.resource != nullptr && frame.mappedData != nullptr)
                {
                    frame.resource->Unmap(0, nullptr);
                }
                frame = {};
            }
            for (FrameBuffer& frame : visualBuffers)
            {
                if (frame.resource != nullptr && frame.mappedData != nullptr)
                {
                    frame.resource->Unmap(0, nullptr);
                }
                frame = {};
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
            imagePaths.clear();
            imageTextures.reset();
            imageMaterial.reset();
            nextImageHandle = 1;
            imageMesh.reset();
            rectangleMesh.reset();
            screenTextRendering = nullptr;
            meshRendering = nullptr;
            device = nullptr;
            lastTexturePassRenderIndex.reset();
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

            if (imagePaths.size() >= MaxTexturesPerSet)
            {
                throw std::length_error(
                    "One Visual2D renderer supports at most 64 shared images.");
            }
            imagePaths.push_back(normalized);
            imageTextures = meshRendering->Textures().LoadTextureSet(imagePaths);
            if (imageMaterial == nullptr)
            {
                imageMaterial = meshRendering->CreateMaterial(
                    BuiltInMaterial::UnlitVertexColorTextureArray);
            }
            imageMaterial->SetTextureSet(imageTextures);

            const std::uint64_t handleValue = nextImageHandle++;
            imageHandlesByPath.emplace(cacheKey, handleValue);
            imagesByHandle.emplace(
                handleValue,
                ImageResource{
                    static_cast<std::uint32_t>(imagePaths.size() - 1)});
            return visual2d::ImageHandle{handleValue};
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

        void EnsureRectangleCapacity(
            const std::uint32_t frameIndex,
            const std::size_t required)
        {
            FrameBuffer& frame = rectangleBuffers[frameIndex];
            if (frame.capacity >= required)
            {
                return;
            }
            if (frame.resource != nullptr && frame.mappedData != nullptr)
            {
                frame.resource->Unmap(0, nullptr);
            }
            frame = {};

            std::size_t capacity = 64;
            while (capacity < required)
            {
                capacity *= 2;
            }
            D3D12_HEAP_PROPERTIES uploadHeap{};
            uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
            const D3D12_RESOURCE_DESC description = BufferDescription(
                capacity * sizeof(RectangleInstance));
            ThrowIfFailed(
                device->CreateCommittedResource(
                    &uploadHeap,
                    D3D12_HEAP_FLAG_NONE,
                    &description,
                    D3D12_RESOURCE_STATE_GENERIC_READ,
                    nullptr,
                    IID_PPV_ARGS(frame.resource.ReleaseAndGetAddressOf())),
                "Create UI rectangle instance buffer");
            ThrowIfFailed(
                frame.resource->Map(
                    0,
                    nullptr,
                    reinterpret_cast<void**>(&frame.mappedData)),
                "Map UI rectangle instance buffer");
            frame.capacity = capacity;
        }

        void DrawRectangles(
            ID3D12GraphicsCommandList& commandList,
            const std::uint32_t frameIndex,
            const std::uint32_t width,
            const std::uint32_t height,
            const std::vector<RectangleInstance>& rectangles)
        {
            if (rectangles.empty())
            {
                return;
            }
            EnsureRectangleCapacity(frameIndex, rectangles.size());
            FrameBuffer& frame = rectangleBuffers[frameIndex];
            std::memcpy(
                frame.mappedData,
                rectangles.data(),
                rectangles.size() * sizeof(RectangleInstance));

            commandList.SetGraphicsRootSignature(
                rectangleRootSignature.Get());
            commandList.SetPipelineState(rectanglePipeline.Get());
            commandList.IASetPrimitiveTopology(
                D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            commandList.SetGraphicsRootShaderResourceView(
                0,
                frame.resource->GetGPUVirtualAddress());
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

        void EnsureVisualCapacity(
            const std::uint32_t frameIndex,
            const std::size_t required)
        {
            FrameBuffer& frame = visualBuffers[frameIndex];
            if (frame.capacity >= required)
            {
                return;
            }
            if (frame.resource != nullptr && frame.mappedData != nullptr)
            {
                frame.resource->Unmap(0, nullptr);
            }
            frame = {};

            std::size_t capacity = 64;
            while (capacity < required)
            {
                capacity *= 2;
            }
            D3D12_HEAP_PROPERTIES uploadHeap{};
            uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
            const D3D12_RESOURCE_DESC description = BufferDescription(
                capacity * sizeof(VisualInstance));
            ThrowIfFailed(
                device->CreateCommittedResource(
                    &uploadHeap,
                    D3D12_HEAP_FLAG_NONE,
                    &description,
                    D3D12_RESOURCE_STATE_GENERIC_READ,
                    nullptr,
                    IID_PPV_ARGS(frame.resource.ReleaseAndGetAddressOf())),
                "Create Visual2D instance buffer");
            ThrowIfFailed(
                frame.resource->Map(
                    0,
                    nullptr,
                    reinterpret_cast<void**>(&frame.mappedData)),
                "Map Visual2D instance buffer");
            frame.capacity = capacity;
        }

        void DrawVisuals(
            ID3D12GraphicsCommandList& commandList,
            const std::uint32_t frameIndex,
            const std::uint32_t width,
            const std::uint32_t height,
            const std::vector<VisualInstance>& visuals)
        {
            if (visuals.empty() || imageTextures == nullptr)
            {
                return;
            }
            EnsureVisualCapacity(frameIndex, visuals.size());
            FrameBuffer& frame = visualBuffers[frameIndex];
            std::memcpy(
                frame.mappedData,
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
                frame.resource->GetGPUVirtualAddress());
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
                imageTextures->gpuDescriptorStart_);
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
        std::vector<std::filesystem::path> imagePaths;
        TextureSetHandle imageTextures;
        MaterialInstanceHandle imageMaterial;
        std::uint64_t nextImageHandle{1};
        FontHandle screenFont;
        TextRenderSystem textureTextRendering;
        FontHandle textureFont;
        ComPtr<ID3D12RootSignature> rectangleRootSignature;
        ComPtr<ID3D12PipelineState> rectanglePipeline;
        std::array<FrameBuffer, D3D12Renderer::FrameCount> rectangleBuffers{};
        ComPtr<ID3D12RootSignature> visualRootSignature;
        ComPtr<ID3D12PipelineState> visualPipeline;
        std::array<FrameBuffer, D3D12Renderer::FrameCount> visualBuffers{};
        std::optional<std::uint64_t> lastTexturePassRenderIndex;
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
                state.meshRendering->Submit(
                    state.imageMesh,
                    state.imageMaterial,
                    world,
                    viewProjection,
                    ToFloat4(command.color),
                    command.uvScale,
                    command.uvOffset,
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
                    state.imageMaterial,
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
        if (state.lastTexturePassRenderIndex.has_value() &&
            *state.lastTexturePassRenderIndex == context.renderIndex)
        {
            throw std::logic_error(
                "One D3D12Visual2DRenderer supports one Canvas texture pass per frame.");
        }
        state.lastTexturePassRenderIndex = context.renderIndex;

        ID3D12GraphicsCommandList& commandList = *context.commandList;
        D3D12_RESOURCE_BARRIER toRenderTarget{};
        toRenderTarget.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toRenderTarget.Transition.pResource = target->resource_.Get();
        toRenderTarget.Transition.Subresource =
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        toRenderTarget.Transition.StateBefore = target->state_;
        toRenderTarget.Transition.StateAfter =
            D3D12_RESOURCE_STATE_RENDER_TARGET;
        commandList.ResourceBarrier(1, &toRenderTarget);
        target->state_ = D3D12_RESOURCE_STATE_RENDER_TARGET;

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
        constexpr float clearColor[4]{0.0F, 0.0F, 0.0F, 0.0F};
        commandList.ClearRenderTargetView(
            target->rtv_, clearColor, 0, nullptr);

        const visual2d::Size canvasSize = canvas.LogicalSize();
        const float scaleX =
            static_cast<float>(target->width_) / canvasSize.width;
        const float scaleY =
            static_cast<float>(target->height_) / canvasSize.height;
        const std::vector<visual2d::DrawPacket> commands = canvas.BuildDrawList();
        std::vector<Impl::RectangleInstance> rectangles;
        rectangles.reserve(commands.size());
        std::vector<Impl::VisualInstance> visuals;
        visuals.reserve(commands.size());

        state.textureTextRendering.BeginFrame(context.frameIndex);
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
                if (state.imageTextures != nullptr)
                {
                    visuals.push_back({
                        {command.bounds.x,
                         command.bounds.y,
                         command.bounds.width,
                         command.bounds.height},
                        ToFloat4(command.color),
                        primitiveTransform,
                        {1.0F, 1.0F, 0.0F, 0.0F},
                        NoTextureIndex});
                }
                else
                {
                    rectangles.push_back({
                        {command.bounds.x,
                         command.bounds.y,
                         command.bounds.width,
                         command.bounds.height},
                        ToFloat4(command.color),
                        primitiveTransform});
                }
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
            state.textureTextRendering.Submit(command.text, text);
        }

        state.DrawRectangles(
            commandList,
            context.frameIndex,
            target->width_,
            target->height_,
            rectangles);
        state.DrawVisuals(
            commandList,
            context.frameIndex,
            target->width_,
            target->height_,
            visuals);
        state.textureTextRendering.Flush(
            commandList,
            target->width_,
            target->height_);

        D3D12_RESOURCE_BARRIER toShaderResource{};
        toShaderResource.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toShaderResource.Transition.pResource = target->resource_.Get();
        toShaderResource.Transition.Subresource =
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        toShaderResource.Transition.StateBefore =
            D3D12_RESOURCE_STATE_RENDER_TARGET;
        toShaderResource.Transition.StateAfter =
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        commandList.ResourceBarrier(1, &toShaderResource);
        target->state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

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
