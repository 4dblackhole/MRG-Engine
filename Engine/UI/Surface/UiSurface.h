#pragma once

#include "Core/UiCanvas.h"
#include "Query/Collision.h"
#include "Shape/Shape.h"

#include <DirectXMath.h>

#include <memory>
#include <optional>
#include <vector>

namespace mrg::ui
{
    struct UiSurfaceHit
    {
        float rayParameter{};
        DirectX::XMFLOAT3 worldPosition{};
        DirectX::XMFLOAT3 worldNormal{};
        DirectX::XMFLOAT2 uv{};
    };

    class IUiSurface
    {
    public:
        virtual ~IUiSurface();
        [[nodiscard]] virtual std::optional<UiSurfaceHit> Raycast(
            const collision::Ray3D& worldRay) const noexcept = 0;
    };

    // Finite XY plane. Local UV (0,0) is the upper-left corner, matching the
    // RectangleShape and canvas coordinate convention.
    class PlaneUiSurface final : public IUiSurface
    {
    public:
        PlaneUiSurface(
            float width,
            float height,
            const DirectX::XMFLOAT4X4& worldTransform,
            bool twoSided = false);

        void SetWorldTransform(
            const DirectX::XMFLOAT4X4& worldTransform) noexcept;
        [[nodiscard]] const DirectX::XMFLOAT4X4& WorldTransform()
            const noexcept;
        [[nodiscard]] UiSize WorldSize() const noexcept;
        [[nodiscard]] std::optional<UiSurfaceHit> Raycast(
            const collision::Ray3D& worldRay) const noexcept override;

    private:
        float width_{};
        float height_{};
        DirectX::XMFLOAT4X4 worldTransform_{};
        bool twoSided_{};
    };

    // Copies CPU positions/UVs from a Shape. This O(triangle-count) baseline
    // is intended for modest interactive surfaces; a later BVH can replace
    // the query internally without changing IUiSurface or client code.
    class MeshUvUiSurface final : public IUiSurface
    {
    public:
        MeshUvUiSurface(
            const geometry::Shape& shape,
            const DirectX::XMFLOAT4X4& worldTransform,
            bool twoSided = false);

        void SetWorldTransform(
            const DirectX::XMFLOAT4X4& worldTransform) noexcept;
        [[nodiscard]] const DirectX::XMFLOAT4X4& WorldTransform()
            const noexcept;
        [[nodiscard]] std::optional<UiSurfaceHit> Raycast(
            const collision::Ray3D& worldRay) const noexcept override;

    private:
        struct SurfaceVertex
        {
            DirectX::XMFLOAT3 position{};
            DirectX::XMFLOAT2 uv{};
        };

        std::vector<SurfaceVertex> vertices_;
        std::vector<std::uint32_t> indices_;
        DirectX::XMFLOAT4X4 worldTransform_{};
        bool twoSided_{};
    };

    // Owns a logical Canvas and composes it with a replaceable world surface.
    // It does not inherit UiCanvas because presentation is not UI ownership.
    class WorldSpaceCanvas final
    {
    public:
        WorldSpaceCanvas(
            UiSize logicalSize,
            std::unique_ptr<IUiSurface> surface);

        [[nodiscard]] UiCanvas& Canvas() noexcept;
        [[nodiscard]] const UiCanvas& Canvas() const noexcept;
        [[nodiscard]] IUiSurface& Surface() noexcept;
        [[nodiscard]] const IUiSurface& Surface() const noexcept;
        void SetSurface(std::unique_ptr<IUiSurface> surface);
        [[nodiscard]] std::optional<UiPoint> MapPointer(
            const collision::Ray3D& worldRay) const noexcept;

    private:
        UiCanvas canvas_;
        std::unique_ptr<IUiSurface> surface_;
    };

    [[nodiscard]] std::optional<UiPoint> MapScreenPointer(
        UiPoint screenPosition,
        UiSize viewportSize,
        UiSize canvasSize,
        UiPoint canvasOrigin = {}) noexcept;

    [[nodiscard]] std::optional<collision::Ray3D> CreateWorldPointerRay(
        UiPoint screenPosition,
        UiSize viewportSize,
        const DirectX::XMFLOAT4X4& viewProjection) noexcept;
}
