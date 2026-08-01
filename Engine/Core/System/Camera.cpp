#include "System/Camera.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace DirectX;

namespace mrg::scene
{
    namespace
    {
        constexpr float PitchMarginRadians = 0.01F;

        [[nodiscard]] bool IsFinitePositive(const float value) noexcept
        {
            return std::isfinite(value) && value > 0.0F;
        }

        void ValidateFieldOfView(const float fieldOfViewRadians)
        {
            if (!std::isfinite(fieldOfViewRadians) ||
                fieldOfViewRadians <= 0.0F ||
                fieldOfViewRadians >= XM_PI)
            {
                throw std::invalid_argument(
                    "The camera vertical field of view is invalid.");
            }
        }

        void ValidatePerspective(
            const float verticalFieldOfViewRadians,
            const float aspectRatio,
            const float nearPlane,
            const float farPlane)
        {
            if (!std::isfinite(verticalFieldOfViewRadians) ||
                verticalFieldOfViewRadians <= 0.0F ||
                verticalFieldOfViewRadians >= XM_PI ||
                !IsFinitePositive(aspectRatio) ||
                !IsFinitePositive(nearPlane) ||
                !std::isfinite(farPlane) ||
                farPlane <= nearPlane)
            {
                throw std::invalid_argument(
                    "Perspective camera parameters are invalid.");
            }
        }

        void ValidateOrthographic(
            const float width,
            const float height,
            const float nearPlane,
            const float farPlane)
        {
            if (!IsFinitePositive(width) || !IsFinitePositive(height) ||
                !std::isfinite(nearPlane) || nearPlane < 0.0F ||
                !std::isfinite(farPlane) || farPlane <= nearPlane)
            {
                throw std::invalid_argument(
                    "Orthographic camera parameters are invalid.");
            }
        }

        [[nodiscard]] XMVECTOR ForwardVector(
            const float yawRadians,
            const float pitchRadians) noexcept
        {
            const float cosPitch = std::cos(pitchRadians);
            return XMVector3Normalize(XMVectorSet(
                std::sin(yawRadians) * cosPitch,
                std::sin(pitchRadians),
                std::cos(yawRadians) * cosPitch,
                0.0F));
        }

        [[nodiscard]] XMVECTOR RightVector(
            const float yawRadians,
            const float pitchRadians) noexcept
        {
            const XMVECTOR worldUp = XMVectorSet(0.0F, 1.0F, 0.0F, 0.0F);
            return XMVector3Normalize(
                XMVector3Cross(worldUp, ForwardVector(yawRadians, pitchRadians)));
        }
    }

    void Camera::SetPosition(
        const float x,
        const float y,
        const float z) noexcept
    {
        position_ = {x, y, z};
    }

    void Camera::SetPosition(const XMFLOAT3& position) noexcept
    {
        position_ = position;
    }

    const XMFLOAT3& Camera::Position() const noexcept
    {
        return position_;
    }

    void Camera::SetYawPitchRadians(
        const float yawRadians,
        const float pitchRadians) noexcept
    {
        yawRadians_ = yawRadians;
        pitchRadians_ = ClampPitch(pitchRadians);
    }

    void Camera::AddYawPitchRadians(
        const float yawDeltaRadians,
        const float pitchDeltaRadians) noexcept
    {
        SetYawPitchRadians(
            yawRadians_ + yawDeltaRadians,
            pitchRadians_ + pitchDeltaRadians);
    }

    float Camera::YawRadians() const noexcept
    {
        return yawRadians_;
    }

    float Camera::PitchRadians() const noexcept
    {
        return pitchRadians_;
    }

    XMFLOAT3 Camera::Forward() const noexcept
    {
        XMFLOAT3 result{};
        XMStoreFloat3(&result, ForwardVector(yawRadians_, pitchRadians_));
        return result;
    }

    XMFLOAT3 Camera::Right() const noexcept
    {
        XMFLOAT3 result{};
        XMStoreFloat3(&result, RightVector(yawRadians_, pitchRadians_));
        return result;
    }

    XMFLOAT3 Camera::Up() const noexcept
    {
        const XMVECTOR forward = ForwardVector(yawRadians_, pitchRadians_);
        const XMVECTOR right = RightVector(yawRadians_, pitchRadians_);
        XMFLOAT3 result{};
        XMStoreFloat3(&result, XMVector3Normalize(XMVector3Cross(forward, right)));
        return result;
    }

    void Camera::SetPerspective(
        const float verticalFieldOfViewRadians,
        const float aspectRatio,
        const float nearPlane,
        const float farPlane)
    {
        ValidatePerspective(
            verticalFieldOfViewRadians,
            aspectRatio,
            nearPlane,
            farPlane);
        projectionType_ = CameraProjectionType::Perspective;
        verticalFieldOfViewRadians_ = verticalFieldOfViewRadians;
        perspectiveAspectRatio_ = aspectRatio;
        nearPlane_ = nearPlane;
        farPlane_ = farPlane;
    }

    void Camera::SetVerticalFieldOfViewRadians(const float fieldOfViewRadians)
    {
        // Perspective values may be prepared while orthographic mode uses a
        // zero near plane, so validate this independent setting by itself.
        ValidateFieldOfView(fieldOfViewRadians);
        verticalFieldOfViewRadians_ = fieldOfViewRadians;
    }

    void Camera::SetPerspectiveAspectRatio(const float aspectRatio)
    {
        if (!IsFinitePositive(aspectRatio))
        {
            throw std::invalid_argument(
                "The camera perspective aspect ratio is invalid.");
        }
        perspectiveAspectRatio_ = aspectRatio;
    }

    void Camera::SetOrthographic(
        const float width,
        const float height,
        const float nearPlane,
        const float farPlane)
    {
        ValidateOrthographic(width, height, nearPlane, farPlane);
        projectionType_ = CameraProjectionType::Orthographic;
        orthographicWidth_ = width;
        orthographicHeight_ = height;
        nearPlane_ = nearPlane;
        farPlane_ = farPlane;
    }

    void Camera::SetOrthographicSize(const float width, const float height)
    {
        ValidateOrthographic(width, height, nearPlane_, farPlane_);
        orthographicWidth_ = width;
        orthographicHeight_ = height;
    }

    void Camera::SetDepthRange(
        const float nearPlane,
        const float farPlane)
    {
        if (projectionType_ == CameraProjectionType::Perspective)
        {
            ValidatePerspective(
                verticalFieldOfViewRadians_,
                perspectiveAspectRatio_,
                nearPlane,
                farPlane);
        }
        else
        {
            ValidateOrthographic(
                orthographicWidth_,
                orthographicHeight_,
                nearPlane,
                farPlane);
        }

        nearPlane_ = nearPlane;
        farPlane_ = farPlane;
    }

    CameraProjectionType Camera::ProjectionType() const noexcept
    {
        return projectionType_;
    }

    float Camera::VerticalFieldOfViewRadians() const noexcept
    {
        return verticalFieldOfViewRadians_;
    }

    float Camera::PerspectiveAspectRatio() const noexcept
    {
        return perspectiveAspectRatio_;
    }

    float Camera::OrthographicWidth() const noexcept
    {
        return orthographicWidth_;
    }

    float Camera::OrthographicHeight() const noexcept
    {
        return orthographicHeight_;
    }

    float Camera::NearPlane() const noexcept
    {
        return nearPlane_;
    }

    float Camera::FarPlane() const noexcept
    {
        return farPlane_;
    }

    XMMATRIX Camera::ViewMatrix() const noexcept
    {
        return XMMatrixLookToLH(
            XMLoadFloat3(&position_),
            ForwardVector(yawRadians_, pitchRadians_),
            XMVectorSet(0.0F, 1.0F, 0.0F, 0.0F));
    }

    XMMATRIX Camera::ProjectionMatrix() const noexcept
    {
        if (projectionType_ == CameraProjectionType::Orthographic)
        {
            return XMMatrixOrthographicLH(
                orthographicWidth_,
                orthographicHeight_,
                nearPlane_,
                farPlane_);
        }

        return XMMatrixPerspectiveFovLH(
            verticalFieldOfViewRadians_,
            perspectiveAspectRatio_,
            nearPlane_,
            farPlane_);
    }

    XMMATRIX Camera::ViewProjectionMatrix() const noexcept
    {
        return ViewMatrix() * ProjectionMatrix();
    }

    float Camera::ClampPitch(const float pitchRadians) noexcept
    {
        return std::clamp(
            pitchRadians,
            -XM_PIDIV2 + PitchMarginRadians,
            XM_PIDIV2 - PitchMarginRadians);
    }
}
