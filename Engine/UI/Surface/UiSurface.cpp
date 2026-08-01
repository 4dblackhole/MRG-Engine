#include "Surface/UiSurface.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace mrg::ui
{
    namespace
    {
        using namespace DirectX;

        struct LocalRay
        {
            collision::Ray3D ray{};
            XMMATRIX world{};
            XMMATRIX inverseWorld{};
        };

        [[nodiscard]] std::optional<LocalRay> ToLocalRay(
            const collision::Ray3D& worldRay,
            const XMFLOAT4X4& worldTransform) noexcept
        {
            const XMMATRIX world = XMLoadFloat4x4(&worldTransform);
            XMVECTOR determinant{};
            const XMMATRIX inverse = XMMatrixInverse(&determinant, world);
            if (std::abs(XMVectorGetX(determinant)) <=
                std::numeric_limits<float>::epsilon())
            {
                return std::nullopt;
            }

            const XMVECTOR origin = XMVector3TransformCoord(
                XMLoadFloat3(&worldRay.origin), inverse);
            const XMVECTOR direction = XMVector3TransformNormal(
                XMLoadFloat3(&worldRay.direction), inverse);
            if (XMVectorGetX(XMVector3LengthSq(direction)) <=
                std::numeric_limits<float>::epsilon())
            {
                return std::nullopt;
            }

            LocalRay result{};
            XMStoreFloat3(&result.ray.origin, origin);
            XMStoreFloat3(&result.ray.direction, direction);
            result.world = world;
            result.inverseWorld = inverse;
            return result;
        }

        [[nodiscard]] XMFLOAT3 TransformNormal(
            const XMFLOAT3& localNormal,
            const XMMATRIX& inverseWorld) noexcept
        {
            XMVECTOR normal = XMVector3TransformNormal(
                XMLoadFloat3(&localNormal),
                XMMatrixTranspose(inverseWorld));
            normal = XMVector3Normalize(normal);
            XMFLOAT3 result{};
            XMStoreFloat3(&result, normal);
            return result;
        }

        [[nodiscard]] float Dot(
            const XMFLOAT3& first,
            const XMFLOAT3& second) noexcept
        {
            return first.x * second.x + first.y * second.y +
                first.z * second.z;
        }
    }

    IUiSurface::~IUiSurface() = default;

    PlaneUiSurface::PlaneUiSurface(
        const float width,
        const float height,
        const DirectX::XMFLOAT4X4& worldTransform,
        const bool twoSided)
        : width_(width), height_(height),
          worldTransform_(worldTransform), twoSided_(twoSided)
    {
        if (!std::isfinite(width_) || !std::isfinite(height_) ||
            width_ <= 0.0F || height_ <= 0.0F)
        {
            throw std::invalid_argument(
                "A plane UI surface requires a finite positive size.");
        }
    }

    void PlaneUiSurface::SetWorldTransform(
        const DirectX::XMFLOAT4X4& worldTransform) noexcept
    {
        worldTransform_ = worldTransform;
    }

    const DirectX::XMFLOAT4X4& PlaneUiSurface::WorldTransform() const noexcept
    {
        return worldTransform_;
    }

    UiSize PlaneUiSurface::WorldSize() const noexcept
    {
        return {width_, height_};
    }

    std::optional<UiSurfaceHit> PlaneUiSurface::Raycast(
        const collision::Ray3D& worldRay) const noexcept
    {
        const std::optional<LocalRay> local =
            ToLocalRay(worldRay, worldTransform_);
        if (!local.has_value())
        {
            return std::nullopt;
        }
        const collision::Plane3D plane{{0.0F, 0.0F, -1.0F}, 0.0F};
        const std::optional<collision::LineHit3D> hit =
            collision::Intersect(plane, local->ray);
        if (!hit.has_value() ||
            (!twoSided_ && Dot(hit->normal, local->ray.direction) >= 0.0F))
        {
            return std::nullopt;
        }

        const float halfWidth = width_ * 0.5F;
        const float halfHeight = height_ * 0.5F;
        if (hit->point.x < -halfWidth || hit->point.x > halfWidth ||
            hit->point.y < -halfHeight || hit->point.y > halfHeight)
        {
            return std::nullopt;
        }

        DirectX::XMFLOAT3 worldPoint{};
        DirectX::XMStoreFloat3(
            &worldPoint,
            DirectX::XMVector3TransformCoord(
                DirectX::XMLoadFloat3(&hit->point), local->world));
        return UiSurfaceHit{
            hit->parameter,
            worldPoint,
            TransformNormal(hit->normal, local->inverseWorld),
            {(hit->point.x + halfWidth) / width_,
                (halfHeight - hit->point.y) / height_}};
    }

    MeshUvUiSurface::MeshUvUiSurface(
        const geometry::Shape& shape,
        const DirectX::XMFLOAT4X4& worldTransform,
        const bool twoSided)
        : worldTransform_(worldTransform), twoSided_(twoSided)
    {
        vertices_.reserve(shape.VertexCount());
        for (const geometry::VertexAttributes& vertex : shape.Vertices())
        {
            vertices_.push_back({vertex.position, vertex.uv});
        }
        indices_.assign(shape.Indices().begin(), shape.Indices().end());
        if (vertices_.empty() || indices_.empty() ||
            indices_.size() % 3 != 0)
        {
            throw std::invalid_argument(
                "A mesh UI surface requires indexed triangles.");
        }
        if (std::ranges::any_of(indices_, [this](const std::uint32_t index)
            {
                return index >= vertices_.size();
            }))
        {
            throw std::invalid_argument(
                "A mesh UI surface contains an invalid vertex index.");
        }
    }

    void MeshUvUiSurface::SetWorldTransform(
        const DirectX::XMFLOAT4X4& worldTransform) noexcept
    {
        worldTransform_ = worldTransform;
    }

    const DirectX::XMFLOAT4X4& MeshUvUiSurface::WorldTransform() const noexcept
    {
        return worldTransform_;
    }

    std::optional<UiSurfaceHit> MeshUvUiSurface::Raycast(
        const collision::Ray3D& worldRay) const noexcept
    {
        const std::optional<LocalRay> local =
            ToLocalRay(worldRay, worldTransform_);
        if (!local.has_value())
        {
            return std::nullopt;
        }

        std::optional<collision::TriangleHit3D> closest;
        const SurfaceVertex* closestVertices[3]{};
        for (std::size_t index = 0; index < indices_.size(); index += 3)
        {
            const SurfaceVertex& first = vertices_[indices_[index]];
            const SurfaceVertex& second = vertices_[indices_[index + 1]];
            const SurfaceVertex& third = vertices_[indices_[index + 2]];
            const collision::Triangle3D triangle{
                first.position, second.position, third.position};
            const std::optional<collision::TriangleHit3D> hit =
                collision::Intersect(triangle, local->ray);
            if (!hit.has_value() ||
                (!twoSided_ && Dot(hit->normal, local->ray.direction) >= 0.0F) ||
                (closest.has_value() && hit->parameter >= closest->parameter))
            {
                continue;
            }
            closest = hit;
            closestVertices[0] = &first;
            closestVertices[1] = &second;
            closestVertices[2] = &third;
        }
        if (!closest.has_value())
        {
            return std::nullopt;
        }

        const DirectX::XMFLOAT3 weights = closest->barycentric;
        const DirectX::XMFLOAT2 uv{
            closestVertices[0]->uv.x * weights.x +
                closestVertices[1]->uv.x * weights.y +
                closestVertices[2]->uv.x * weights.z,
            closestVertices[0]->uv.y * weights.x +
                closestVertices[1]->uv.y * weights.y +
                closestVertices[2]->uv.y * weights.z};
        DirectX::XMFLOAT3 worldPoint{};
        DirectX::XMStoreFloat3(
            &worldPoint,
            DirectX::XMVector3TransformCoord(
                DirectX::XMLoadFloat3(&closest->point), local->world));
        return UiSurfaceHit{
            closest->parameter,
            worldPoint,
            TransformNormal(closest->normal, local->inverseWorld),
            uv};
    }

    WorldSpaceCanvas::WorldSpaceCanvas(
        const UiSize logicalSize,
        std::unique_ptr<IUiSurface> surface)
        : canvas_(logicalSize), surface_(std::move(surface))
    {
        if (surface_ == nullptr)
        {
            throw std::invalid_argument(
                "A world-space canvas requires a UI surface.");
        }
    }

    UiCanvas& WorldSpaceCanvas::Canvas() noexcept
    {
        return canvas_;
    }

    const UiCanvas& WorldSpaceCanvas::Canvas() const noexcept
    {
        return canvas_;
    }

    IUiSurface& WorldSpaceCanvas::Surface() noexcept
    {
        return *surface_;
    }

    const IUiSurface& WorldSpaceCanvas::Surface() const noexcept
    {
        return *surface_;
    }

    void WorldSpaceCanvas::SetSurface(std::unique_ptr<IUiSurface> surface)
    {
        if (surface == nullptr)
        {
            throw std::invalid_argument("A UI surface cannot be null.");
        }
        surface_ = std::move(surface);
    }

    std::optional<UiPoint> WorldSpaceCanvas::MapPointer(
        const collision::Ray3D& worldRay) const noexcept
    {
        const std::optional<UiSurfaceHit> hit = surface_->Raycast(worldRay);
        if (!hit.has_value())
        {
            return std::nullopt;
        }
        const UiSize size = canvas_.LogicalSize();
        return UiPoint{hit->uv.x * size.width, hit->uv.y * size.height};
    }

    std::optional<UiPoint> MapScreenPointer(
        const UiPoint screenPosition,
        const UiSize viewportSize,
        const UiSize canvasSize,
        const UiPoint canvasOrigin) noexcept
    {
        if (viewportSize.width <= 0.0F || viewportSize.height <= 0.0F ||
            canvasSize.width <= 0.0F || canvasSize.height <= 0.0F)
        {
            return std::nullopt;
        }
        if (screenPosition.x < 0.0F || screenPosition.y < 0.0F ||
            screenPosition.x > viewportSize.width ||
            screenPosition.y > viewportSize.height)
        {
            return std::nullopt;
        }
        if (screenPosition.x < canvasOrigin.x ||
            screenPosition.y < canvasOrigin.y ||
            screenPosition.x > canvasOrigin.x + canvasSize.width ||
            screenPosition.y > canvasOrigin.y + canvasSize.height)
        {
            return std::nullopt;
        }
        return UiPoint{
            screenPosition.x - canvasOrigin.x,
            screenPosition.y - canvasOrigin.y};
    }

    std::optional<collision::Ray3D> CreateWorldPointerRay(
        const UiPoint screenPosition,
        const UiSize viewportSize,
        const DirectX::XMFLOAT4X4& viewProjection) noexcept
    {
        if (viewportSize.width <= 0.0F || viewportSize.height <= 0.0F)
        {
            return std::nullopt;
        }
        const float normalizedX =
            screenPosition.x * 2.0F / viewportSize.width - 1.0F;
        const float normalizedY =
            1.0F - screenPosition.y * 2.0F / viewportSize.height;

        DirectX::XMVECTOR determinant{};
        const DirectX::XMMATRIX inverse = DirectX::XMMatrixInverse(
            &determinant,
            DirectX::XMLoadFloat4x4(&viewProjection));
        if (std::abs(DirectX::XMVectorGetX(determinant)) <=
            std::numeric_limits<float>::epsilon())
        {
            return std::nullopt;
        }

        const DirectX::XMVECTOR nearPoint = DirectX::XMVector3TransformCoord(
            DirectX::XMVectorSet(normalizedX, normalizedY, 0.0F, 1.0F),
            inverse);
        const DirectX::XMVECTOR farPoint = DirectX::XMVector3TransformCoord(
            DirectX::XMVectorSet(normalizedX, normalizedY, 1.0F, 1.0F),
            inverse);
        DirectX::XMVECTOR direction = DirectX::XMVectorSubtract(
            farPoint, nearPoint);
        if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(direction)) <=
            std::numeric_limits<float>::epsilon())
        {
            return std::nullopt;
        }
        direction = DirectX::XMVector3Normalize(direction);

        collision::Ray3D result{};
        DirectX::XMStoreFloat3(&result.origin, nearPoint);
        DirectX::XMStoreFloat3(&result.direction, direction);
        return result;
    }
}
