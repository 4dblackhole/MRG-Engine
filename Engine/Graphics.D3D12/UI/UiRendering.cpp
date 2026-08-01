#include "UI/UiRendering.h"

#include "Primitive/RectangleShape.h"

#include <algorithm>
#include <stdexcept>

namespace mrg::graphics
{
    namespace
    {
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
    }

    void D3D12UiRenderer::Initialize(
        MeshRenderSystem& meshRendering,
        TextRenderSystem& textRendering)
    {
        if (IsInitialized())
        {
            throw std::logic_error("The D3D12 UI renderer is already initialized.");
        }
        meshRendering_ = &meshRendering;
        textRendering_ = &textRendering;

        const geometry::RectangleShape rectangle;
        rectangleMesh_ = meshRendering.CreateMesh<
            geometry::VertexPositionColor>(rectangle);
        rectangleMaterial_ = meshRendering.CreateMaterial(
            BuiltInMaterial::UnlitVertexColor);
        defaultFont_ = textRendering.LoadSystemFont(L"Segoe UI");
    }

    void D3D12UiRenderer::Shutdown() noexcept
    {
        defaultFont_.reset();
        rectangleMaterial_.reset();
        rectangleMesh_.reset();
        textRendering_ = nullptr;
        meshRendering_ = nullptr;
    }

    void D3D12UiRenderer::SubmitScreen(
        const ui::UiCanvas& canvas,
        const RenderContext& context,
        const ui::UiPoint screenOrigin)
    {
        if (!IsInitialized() || context.meshRendering != meshRendering_ ||
            context.textRendering != textRendering_)
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
                text.style.font = defaultFont_;
                text.style.fontSizePixels = command.fontSize;
                text.style.color = ToFloat4(command.color);
                textRendering_->Submit(command.text, text);
                continue;
            }
            if (bounds.width <= 0.0F || bounds.height <= 0.0F)
            {
                continue;
            }

            // Later retained-mode commands sit slightly nearer so children
            // reliably cover parents while all items remain one mesh batch.
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
            meshRendering_->Submit(
                rectangleMesh_,
                rectangleMaterial_,
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
        if (!IsInitialized() || context.meshRendering != meshRendering_)
        {
            throw std::logic_error(
                "The D3D12 UI renderer is not initialized for this context.");
        }
        if (surfaceWorldSize.width <= 0.0F || surfaceWorldSize.height <= 0.0F)
        {
            throw std::invalid_argument("A world UI plane must have positive size.");
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

            const float width =
                command.bounds.width / canvasSize.width * surfaceWorldSize.width;
            const float height =
                command.bounds.height / canvasSize.height * surfaceWorldSize.height;
            const float centerX =
                (command.bounds.x + command.bounds.width * 0.5F) /
                    canvasSize.width * surfaceWorldSize.width -
                surfaceWorldSize.width * 0.5F;
            const float centerY =
                surfaceWorldSize.height * 0.5F -
                (command.bounds.y + command.bounds.height * 0.5F) /
                    canvasSize.height * surfaceWorldSize.height;
            const float layer = -static_cast<float>(index) * 0.0005F;
            DirectX::XMFLOAT4X4 world{};
            DirectX::XMStoreFloat4x4(
                &world,
                DirectX::XMMatrixScaling(width, height, 1.0F) *
                DirectX::XMMatrixTranslation(centerX, centerY, layer) *
                surface);
            meshRendering_->Submit(
                rectangleMesh_,
                rectangleMaterial_,
                world,
                viewProjection,
                ToFloat4(command.color),
                {1.0F, 1.0F},
                {0.0F, 0.0F},
                NoTextureIndex);
        }
    }

    bool D3D12UiRenderer::IsInitialized() const noexcept
    {
        return meshRendering_ != nullptr && textRendering_ != nullptr &&
            rectangleMesh_ != nullptr && rectangleMaterial_ != nullptr &&
            defaultFont_ != nullptr;
    }
}
