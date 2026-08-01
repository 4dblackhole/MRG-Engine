#pragma once

// D3D12 presentation adapter for backend-neutral UiCanvas draw commands.

#include "Core/UiCanvas.h"
#include "Mesh/MeshRendering.h"
#include "Renderer/D3D12Renderer.h"
#include "Text/TextRendering.h"

#include <DirectXMath.h>

namespace mrg::graphics
{
    class D3D12UiRenderer final
    {
    public:
        D3D12UiRenderer() = default;

        void Initialize(
            MeshRenderSystem& meshRendering,
            TextRenderSystem& textRendering);
        void Shutdown() noexcept;

        // Renders at pixel size with a top-left screen origin. Text is
        // supported by the existing DirectWrite-backed screen renderer.
        void SubmitScreen(
            const ui::UiCanvas& canvas,
            const RenderContext& context,
            ui::UiPoint screenOrigin = {});

        // Renders rectangles directly onto a finite local XY plane. The
        // transform positions that plane in the world. Rich text and curved
        // visual warping require a canvas-to-texture presenter; input mapping
        // remains fully supported by PlaneUiSurface/MeshUvUiSurface.
        void SubmitPlane(
            const ui::UiCanvas& canvas,
            const RenderContext& context,
            const DirectX::XMFLOAT4X4& surfaceWorld,
            ui::UiSize surfaceWorldSize,
            const DirectX::XMFLOAT4X4& viewProjection);

    private:
        [[nodiscard]] bool IsInitialized() const noexcept;

        MeshRenderSystem* meshRendering_{};
        TextRenderSystem* textRendering_{};
        GpuMeshHandle rectangleMesh_;
        MaterialInstanceHandle rectangleMaterial_;
        FontHandle defaultFont_;
    };
}
