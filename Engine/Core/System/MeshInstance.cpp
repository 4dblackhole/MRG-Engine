#include "System/MeshInstance.h"

#include <stdexcept>
#include <utility>

using namespace DirectX;

namespace mrg::scene
{
    MeshInstance::MeshInstance(
        graphics::GpuMeshHandle mesh,
        graphics::MaterialInstanceHandle material)
    {
        SetMesh(std::move(mesh));
        SetMaterial(std::move(material));
    }

    void MeshInstance::SetMesh(graphics::GpuMeshHandle mesh)
    {
        if (mesh == nullptr)
        {
            throw std::invalid_argument(
                "A MeshInstance mesh cannot be null.");
        }
        mesh_ = std::move(mesh);
    }

    void MeshInstance::SetMaterial(
        graphics::MaterialInstanceHandle material)
    {
        if (material == nullptr)
        {
            throw std::invalid_argument(
                "A MeshInstance material cannot be null.");
        }
        material_ = std::move(material);
    }

    void MeshInstance::Reset() noexcept
    {
        material_.reset();
        mesh_.reset();
        color_ = {1.0F, 1.0F, 1.0F, 1.0F};
        uvScale_ = {1.0F, 1.0F};
        uvOffset_ = {0.0F, 0.0F};
        textureIndex_ = graphics::NoTextureIndex;
    }

    const graphics::GpuMeshHandle& MeshInstance::Mesh() const noexcept
    {
        return mesh_;
    }

    const graphics::MaterialInstanceHandle&
        MeshInstance::Material() const noexcept
    {
        return material_;
    }

    TransformNode& MeshInstance::Transform() noexcept
    {
        return transform_;
    }

    const TransformNode& MeshInstance::Transform() const noexcept
    {
        return transform_;
    }

    void MeshInstance::SetColor(const XMFLOAT4& color) noexcept
    {
        color_ = color;
    }

    const XMFLOAT4& MeshInstance::Color() const noexcept
    {
        return color_;
    }

    void MeshInstance::SetTextureIndex(
        const std::uint32_t textureIndex) noexcept
    {
        textureIndex_ = textureIndex;
    }

    void MeshInstance::ClearTexture() noexcept
    {
        textureIndex_ = graphics::NoTextureIndex;
        uvScale_ = {1.0F, 1.0F};
        uvOffset_ = {0.0F, 0.0F};
    }

    std::uint32_t MeshInstance::TextureIndex() const noexcept
    {
        return textureIndex_;
    }

    void MeshInstance::SetUvTransform(
        const XMFLOAT2& scale,
        const XMFLOAT2& offset) noexcept
    {
        uvScale_ = scale;
        uvOffset_ = offset;
    }

    void MeshInstance::SetUvTransform(
        const graphics::UvTransform& transform) noexcept
    {
        SetUvTransform(transform.scale, transform.offset);
    }

    const XMFLOAT2& MeshInstance::UvScale() const noexcept
    {
        return uvScale_;
    }

    const XMFLOAT2& MeshInstance::UvOffset() const noexcept
    {
        return uvOffset_;
    }

    bool MeshInstance::IsReady() const noexcept
    {
        return mesh_ != nullptr && material_ != nullptr;
    }

    void MeshInstance::Submit(
        const graphics::RenderContext& context,
        const Camera& camera)
    {
        if (!IsReady())
        {
            throw std::logic_error(
                "MeshInstance must have a mesh and material before Submit.");
        }
        if (context.meshRendering == nullptr)
        {
            throw std::logic_error(
                "RenderContext has no mesh rendering service.");
        }

        XMFLOAT4X4 viewProjection{};
        XMStoreFloat4x4(
            &viewProjection,
            camera.ViewProjectionMatrix());
        context.meshRendering->Submit(
            mesh_,
            material_,
            transform_.WorldMatrix(),
            viewProjection,
            color_,
            uvScale_,
            uvOffset_,
            textureIndex_);
    }
}
