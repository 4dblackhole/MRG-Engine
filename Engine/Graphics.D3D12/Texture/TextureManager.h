#pragma once

// Texture feature: WIC decoding, uploads, and SRV descriptor sets.

#include <DirectXMath.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace mrg::graphics
{
    class D3D12UiRenderer;
    // One descriptor table reserves this many entries.  Each entry can point
    // at an independently sized Texture2D resource; this is not a
    // D3D12 Texture2DArray and therefore does not require equal dimensions.
    inline constexpr std::uint32_t MaxTexturesPerSet = 64;

    struct TextureInfo final
    {
        std::uint32_t width{};
        std::uint32_t height{};
    };

    struct UvTransform final
    {
        DirectX::XMFLOAT2 scale{1.0F, 1.0F};
        DirectX::XMFLOAT2 offset{0.0F, 0.0F};
    };

    class TextureSet final
    {
    public:
        TextureSet(const TextureSet&) = delete;
        TextureSet& operator=(const TextureSet&) = delete;

        [[nodiscard]] std::size_t Size() const noexcept;
        [[nodiscard]] const TextureInfo& Info(std::size_t index) const;

        // Returns a centered "cover" transform.  Sampling a rectangular
        // image on the requested aspect ratio crops the excess dimension,
        // filling the complete target without stretching or letterboxing.
        [[nodiscard]] UvTransform MakeCoverUvTransform(
            std::size_t index,
            float targetAspectRatio = 1.0F) const;

    private:
        friend class MeshRenderSystem;
        friend class TextureManager;

        TextureSet() = default;

        std::vector<TextureInfo> textureInfo_;
        std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> resources_;
        D3D12_GPU_DESCRIPTOR_HANDLE gpuDescriptorStart_{};
    };

    using TextureSetHandle = std::shared_ptr<const TextureSet>;

    // One GPU texture that can alternate between a render target and a
    // shader resource. TextureManager owns its SRV allocation while the
    // target keeps the RTV and resource state required by a render pass.
    class RenderTargetTexture final
    {
    public:
        RenderTargetTexture(const RenderTargetTexture&) = delete;
        RenderTargetTexture& operator=(const RenderTargetTexture&) = delete;

        [[nodiscard]] const TextureSetHandle& Textures() const noexcept;
        [[nodiscard]] std::uint32_t Width() const noexcept;
        [[nodiscard]] std::uint32_t Height() const noexcept;

    private:
        friend class D3D12UiRenderer;
        friend class TextureManager;

        RenderTargetTexture() = default;

        TextureSetHandle textures_;
        Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv_{};
        D3D12_RESOURCE_STATES state_{
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
        std::uint32_t width_{};
        std::uint32_t height_{};
    };

    using RenderTargetTextureHandle =
        std::shared_ptr<RenderTargetTexture>;

    // Decodes common image formats through WIC and owns the shader-visible
    // descriptor heap used by textured materials.  Initial uploads are
    // synchronous so temporary upload buffers can be released immediately;
    // no upload wait is added to the normal render loop.
    class TextureManager final
    {
    public:
        TextureManager() = default;
        ~TextureManager();

        TextureManager(const TextureManager&) = delete;
        TextureManager& operator=(const TextureManager&) = delete;

        void Initialize(
            ID3D12Device& device,
            ID3D12CommandQueue& commandQueue);
        void Shutdown() noexcept;

        [[nodiscard]] TextureSetHandle LoadTextureSet(
            std::span<const std::filesystem::path> paths);
        [[nodiscard]] RenderTargetTextureHandle CreateRenderTargetTexture(
            std::uint32_t width,
            std::uint32_t height);

        [[nodiscard]] ID3D12DescriptorHeap* DescriptorHeap() const noexcept;

    private:
        struct DecodedImage;

        void ValidateTextureSetRequest(
            std::span<const std::filesystem::path> paths) const;
        [[nodiscard]] static DecodedImage DecodeImage(
            const std::filesystem::path& path);
        void BeginUploadCommands();
        [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE
            InitializeDescriptorBlock(TextureSet& textureSet);
        void RecordTextureUpload(
            const DecodedImage& image,
            std::size_t descriptorIndex,
            D3D12_CPU_DESCRIPTOR_HANDLE descriptorBlockStart,
            TextureSet& textureSet,
            std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>&
                uploadBuffers);
        void ExecuteUploadAndWait();

        ID3D12Device* device_{};
        ID3D12CommandQueue* commandQueue_{};
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap_;
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> uploadAllocator_;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> uploadCommandList_;
        Microsoft::WRL::ComPtr<ID3D12Fence> uploadFence_;
        HANDLE uploadFenceEvent_{};
        std::uint64_t nextUploadFenceValue_{1};
        std::uint32_t descriptorSize_{};
        std::uint32_t nextDescriptorBlock_{};
        bool initialized_{};
    };
}
