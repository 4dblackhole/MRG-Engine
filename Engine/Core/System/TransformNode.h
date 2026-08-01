#pragma once

// System feature: reusable hierarchical local and world transforms.

#include <DirectXMath.h>

#include <memory>
#include <vector>

namespace mrg::scene
{
    // Public hierarchical local/world transform used by Client scene objects.
    class TransformNode final
    {
    public:
        TransformNode();

        TransformNode(const TransformNode&) = delete;
        TransformNode& operator=(const TransformNode&) = delete;

        void SetPosition(float x, float y, float z) noexcept;
        void SetScale(float x, float y, float z) noexcept;
        void SetRotationRollPitchYaw(
            float pitch,
            float yaw,
            float roll) noexcept;
        void SetRotationQuaternion(
            float x,
            float y,
            float z,
            float w) noexcept;

        [[nodiscard]] const DirectX::XMFLOAT3& Position() const noexcept;
        [[nodiscard]] const DirectX::XMFLOAT3& Scale() const noexcept;
        [[nodiscard]] const DirectX::XMFLOAT4& Rotation() const noexcept;

        TransformNode& AddChild(std::unique_ptr<TransformNode> child);
        [[nodiscard]] TransformNode& CreateChild();
        [[nodiscard]] TransformNode* Parent() const noexcept;
        [[nodiscard]] const std::vector<std::unique_ptr<TransformNode>>&
            Children() const noexcept;

        [[nodiscard]] const DirectX::XMFLOAT4X4& WorldMatrix();
        void UpdateWorldRecursive();

    private:
        void MarkWorldDirty() noexcept;
        void UpdateWorld();

        DirectX::XMFLOAT3 position_{0.0F, 0.0F, 0.0F};
        DirectX::XMFLOAT3 scale_{1.0F, 1.0F, 1.0F};
        DirectX::XMFLOAT4 rotation_{0.0F, 0.0F, 0.0F, 1.0F};
        DirectX::XMFLOAT4X4 world_{};
        TransformNode* parent_{};
        std::vector<std::unique_ptr<TransformNode>> children_;
        bool worldDirty_{true};
    };
}
