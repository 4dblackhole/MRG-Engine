#pragma once

// D3D12 presentation adapter for backend-neutral Visual2DCanvas draw commands.

#include "Visual2D/Visual2DCanvas.h"
#include "Mesh/MeshRendering.h"
#include "Renderer/D3D12Renderer.h"
#include "Text/TextRendering.h"

#include <DirectXMath.h>

#include <cstdint>
#include <filesystem>
#include <memory>

namespace mrg::graphics
{
    class D3D12Visual2DRenderer final
    {
    public:
        D3D12Visual2DRenderer();
        ~D3D12Visual2DRenderer();

        D3D12Visual2DRenderer(const D3D12Visual2DRenderer&) = delete;
        D3D12Visual2DRenderer& operator=(const D3D12Visual2DRenderer&) = delete;

        void Initialize(
            MeshRenderSystem& meshRendering,
            TextRenderSystem& textRendering);
        void Shutdown() noexcept;

        // Loads a PNG/WIC-supported image once and returns an opaque handle
        // that can be assigned to a SpriteVisualComponent or widget style.
        [[nodiscard]] visual2d::ImageHandle LoadImage(
            const std::filesystem::path& path);

        // Renders at pixel size with a top-left screen origin. A larger Canvas
        // Z-order places the Canvas and its complete element tree in front of
        // a smaller one. Values above 31 are clamped to the front-most band.
        void SubmitScreen(
            const visual2d::Visual2DCanvas& canvas,
            const RenderContext& context,
            visual2d::Point screenOrigin = {},
            std::uint32_t canvasZOrder = 0);

        // Renders rectangles directly onto a finite local XY plane. Use
        // RenderToTexture plus a textured mesh when text or curvature is
        // required. Input mapping remains independent of either path.
        void SubmitPlane(
            const visual2d::Visual2DCanvas& canvas,
            const RenderContext& context,
            const DirectX::XMFLOAT4X4& surfaceWorld,
            visual2d::Size surfaceWorldSize,
            const DirectX::XMFLOAT4X4& viewProjection);

        [[nodiscard]] RenderTargetTextureHandle CreateCanvasRenderTarget(
            std::uint32_t width,
            std::uint32_t height);

        // Records an immediate off-screen pass. Rectangles, images, and
        // DirectWrite glyphs are rendered into target, transitioned to an
        // SRV, and can then be sampled by a curved mesh later in the frame.
        void RenderToTexture(
            const visual2d::Visual2DCanvas& canvas,
            const RenderTargetTextureHandle& target,
            const RenderContext& context);

    private:
        [[nodiscard]] bool IsInitialized() const noexcept;

        struct Impl;
        std::unique_ptr<Impl> implementation_;
    };
}
