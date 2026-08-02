#include "UI/UiRendering.h"

#include "Primitive/RectangleShape.h"
#include "Shader/ShaderCompiler.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace mrg::graphics
{
    namespace
    {
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
            const ui::UiColor color) noexcept
        {
            return {color.red, color.green, color.blue, color.alpha};
        }

        [[nodiscard]] TextHorizontalAlignment ToTextAlignment(
            const ui::UiTextAlignment alignment) noexcept
        {
            switch (alignment)
            {
            case ui::UiTextAlignment::Center:
                return TextHorizontalAlignment::Center;
            case ui::UiTextAlignment::Trailing:
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

    struct D3D12UiRenderer::Impl
    {
        struct RectangleInstance
        {
            DirectX::XMFLOAT2 positionPixels{};
            DirectX::XMFLOAT2 sizePixels{};
            DirectX::XMFLOAT4 color{};
        };

        struct FrameBuffer
        {
            ComPtr<ID3D12Resource> resource;
            std::byte* mappedData{};
            std::size_t capacity{};
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
                    "The D3D12 UI renderer is already initialized.");
            }
            if (meshes.Device() == nullptr)
            {
                throw std::logic_error(
                    "The mesh renderer has no D3D12 device.");
            }

            meshRendering = &meshes;
            screenTextRendering = &text;
            device = meshes.Device();

            const geometry::RectangleShape rectangle;
            rectangleMesh = meshes.CreateMesh<
                geometry::VertexPositionColor>(rectangle);
            rectangleMaterial = meshes.CreateMaterial(
                BuiltInMaterial::UnlitVertexColor);
            screenFont = text.LoadSystemFont(L"Segoe UI");

            textureTextRendering.Initialize(
                *device,
                DXGI_FORMAT_R8G8B8A8_UNORM);
            textureFont = textureTextRendering.LoadSystemFont(L"Segoe UI");
            CreateRectanglePipeline();
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
            rectanglePipeline.Reset();
            rectangleRootSignature.Reset();
            textureFont.reset();
            textureTextRendering.Shutdown();
            screenFont.reset();
            rectangleMaterial.reset();
            rectangleMesh.reset();
            screenTextRendering = nullptr;
            meshRendering = nullptr;
            device = nullptr;
            lastTexturePassRenderIndex.reset();
            initialized = false;
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
                detail::BuiltInShader::UiRectangle,
                "VSMain",
                "vs_5_1");
            const ComPtr<ID3DBlob> pixelShader = detail::CompileShader(
                detail::BuiltInShader::UiRectangle,
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

        MeshRenderSystem* meshRendering{};
        TextRenderSystem* screenTextRendering{};
        ID3D12Device* device{};
        GpuMeshHandle rectangleMesh;
        MaterialInstanceHandle rectangleMaterial;
        FontHandle screenFont;
        TextRenderSystem textureTextRendering;
        FontHandle textureFont;
        ComPtr<ID3D12RootSignature> rectangleRootSignature;
        ComPtr<ID3D12PipelineState> rectanglePipeline;
        std::array<FrameBuffer, D3D12Renderer::FrameCount> rectangleBuffers{};
        std::optional<std::uint64_t> lastTexturePassRenderIndex;
        bool initialized{};
    };

    D3D12UiRenderer::D3D12UiRenderer()
        : implementation_(std::make_unique<Impl>())
    {
    }

    D3D12UiRenderer::~D3D12UiRenderer() = default;

    void D3D12UiRenderer::Initialize(
        MeshRenderSystem& meshRendering,
        TextRenderSystem& textRendering)
    {
        implementation_->Initialize(meshRendering, textRendering);
    }

    void D3D12UiRenderer::Shutdown() noexcept
    {
        implementation_->Shutdown();
    }

    void D3D12UiRenderer::SubmitScreen(
        const ui::UiCanvas& canvas,
        const RenderContext& context,
        const ui::UiPoint screenOrigin)
    {
        Impl& state = *implementation_;
        if (!IsInitialized() || context.meshRendering != state.meshRendering ||
            context.textRendering != state.screenTextRendering)
        {
            throw std::logic_error(
                "The D3D12 UI renderer is not initialized for this context.");
        }

        const std::vector<ui::UiDrawCommand> commands = canvas.BuildDrawList();
        DirectX::XMFLOAT4X4 viewProjection{};
        DirectX::XMStoreFloat4x4(
            &viewProjection,
            DirectX::XMMatrixOrthographicOffCenterLH(
                0.0F,
                static_cast<float>(context.width),
                0.0F,
                static_cast<float>(context.height),
                0.0F,
                1.0F));

        for (std::size_t index = 0; index < commands.size(); ++index)
        {
            const ui::UiDrawCommand& command = commands[index];
            const ui::UiRect bounds{
                screenOrigin.x + command.bounds.x,
                screenOrigin.y + command.bounds.y,
                command.bounds.width,
                command.bounds.height};
            if (command.type == ui::UiDrawCommandType::Text)
            {
                TextDrawCommand text{};
                text.positionPixels = {bounds.x, bounds.y};
                text.layoutSizePixels = {bounds.width, bounds.height};
                text.horizontalAlignment = ToTextAlignment(
                    command.horizontalAlignment);
                text.verticalAlignment = TextVerticalAlignment::Center;
                text.style.font = state.screenFont;
                text.style.fontSizePixels = command.fontSize;
                text.style.color = ToFloat4(command.color);
                state.screenTextRendering->Submit(command.text, text);
                continue;
            }
            if (bounds.width <= 0.0F || bounds.height <= 0.0F)
            {
                continue;
            }

            const float normalizedOrder = commands.empty()
                ? 0.0F
                : static_cast<float>(index + 1) /
                    static_cast<float>(commands.size() + 1);
            const float depth = 0.10F * (1.0F - normalizedOrder);
            DirectX::XMFLOAT4X4 world{};
            DirectX::XMStoreFloat4x4(
                &world,
                DirectX::XMMatrixScaling(
                    bounds.width,
                    bounds.height,
                    1.0F) *
                DirectX::XMMatrixTranslation(
                    bounds.x + bounds.width * 0.5F,
                    static_cast<float>(context.height) -
                        bounds.y - bounds.height * 0.5F,
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

    void D3D12UiRenderer::SubmitPlane(
        const ui::UiCanvas& canvas,
        const RenderContext& context,
        const DirectX::XMFLOAT4X4& surfaceWorld,
        const ui::UiSize surfaceWorldSize,
        const DirectX::XMFLOAT4X4& viewProjection)
    {
        Impl& state = *implementation_;
        if (!IsInitialized() || context.meshRendering != state.meshRendering)
        {
            throw std::logic_error(
                "The D3D12 UI renderer is not initialized for this context.");
        }
        if (surfaceWorldSize.width <= 0.0F || surfaceWorldSize.height <= 0.0F)
        {
            throw std::invalid_argument(
                "A world UI plane must have positive size.");
        }

        const ui::UiSize canvasSize = canvas.LogicalSize();
        const std::vector<ui::UiDrawCommand> commands = canvas.BuildDrawList();
        const DirectX::XMMATRIX surface =
            DirectX::XMLoadFloat4x4(&surfaceWorld);
        for (std::size_t index = 0; index < commands.size(); ++index)
        {
            const ui::UiDrawCommand& command = commands[index];
            if (command.type != ui::UiDrawCommandType::Rectangle ||
                command.bounds.width <= 0.0F || command.bounds.height <= 0.0F)
            {
                continue;
            }

            const float width = command.bounds.width / canvasSize.width *
                surfaceWorldSize.width;
            const float height = command.bounds.height / canvasSize.height *
                surfaceWorldSize.height;
            const float centerX =
                (command.bounds.x + command.bounds.width * 0.5F) /
                    canvasSize.width * surfaceWorldSize.width -
                surfaceWorldSize.width * 0.5F;
            const float centerY = surfaceWorldSize.height * 0.5F -
                (command.bounds.y + command.bounds.height * 0.5F) /
                    canvasSize.height * surfaceWorldSize.height;
            const float layer = -static_cast<float>(index) * 0.0005F;
            DirectX::XMFLOAT4X4 world{};
            DirectX::XMStoreFloat4x4(
                &world,
                DirectX::XMMatrixScaling(width, height, 1.0F) *
                DirectX::XMMatrixTranslation(centerX, centerY, layer) *
                surface);
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

    RenderTargetTextureHandle D3D12UiRenderer::CreateCanvasRenderTarget(
        const std::uint32_t width,
        const std::uint32_t height)
    {
        if (!IsInitialized())
        {
            throw std::logic_error(
                "The D3D12 UI renderer must be initialized first.");
        }
        return implementation_->meshRendering->Textures().
            CreateRenderTargetTexture(width, height);
    }

    void D3D12UiRenderer::RenderToTexture(
        const ui::UiCanvas& canvas,
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
                "One D3D12UiRenderer supports one Canvas texture pass per frame.");
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

        const ui::UiSize canvasSize = canvas.LogicalSize();
        const float scaleX =
            static_cast<float>(target->width_) / canvasSize.width;
        const float scaleY =
            static_cast<float>(target->height_) / canvasSize.height;
        const std::vector<ui::UiDrawCommand> commands = canvas.BuildDrawList();
        std::vector<Impl::RectangleInstance> rectangles;
        rectangles.reserve(commands.size());

        state.textureTextRendering.BeginFrame(context.frameIndex);
        for (const ui::UiDrawCommand& command : commands)
        {
            const ui::UiRect bounds{
                command.bounds.x * scaleX,
                command.bounds.y * scaleY,
                command.bounds.width * scaleX,
                command.bounds.height * scaleY};
            if (command.type == ui::UiDrawCommandType::Rectangle)
            {
                rectangles.push_back({
                    {bounds.x, bounds.y},
                    {bounds.width, bounds.height},
                    ToFloat4(command.color)});
                continue;
            }

            TextDrawCommand text{};
            text.positionPixels = {bounds.x, bounds.y};
            text.layoutSizePixels = {bounds.width, bounds.height};
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

    bool D3D12UiRenderer::IsInitialized() const noexcept
    {
        return implementation_ != nullptr && implementation_->initialized;
    }
}
