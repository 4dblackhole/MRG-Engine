#pragma once

// DirectWrite-backed text layout with a native D3D12 glyph-atlas renderer.
// Public types intentionally hide DirectWrite objects and atlas resources.

#include <DirectXMath.h>
#include <d3d12.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>

namespace mrg::graphics
{
    enum class TextHorizontalAlignment : std::uint8_t
    {
        Leading,
        Center,
        Trailing,
    };

    enum class TextVerticalAlignment : std::uint8_t
    {
        Near,
        Center,
        Far,
    };

    class Font final
    {
    public:
        ~Font();

        Font(const Font&) = delete;
        Font& operator=(const Font&) = delete;

        [[nodiscard]] std::wstring_view FamilyName() const noexcept;

    private:
        friend class TextRenderSystem;
        struct Impl;

        explicit Font(std::shared_ptr<Impl> implementation);
        std::shared_ptr<Impl> implementation_;
    };

    using FontHandle = std::shared_ptr<const Font>;

    struct TextStyle
    {
        FontHandle font;
        float fontSizePixels{20.0F};
        DirectX::XMFLOAT4 color{1.0F, 1.0F, 1.0F, 1.0F};
    };

    struct TextDrawCommand
    {
        DirectX::XMFLOAT2 positionPixels{};
        DirectX::XMFLOAT2 layoutSizePixels{512.0F, 128.0F};
        // Smaller values are closer to the viewer. Screen UI supplies this
        // from its sorted draw-command order so popup text obeys the same
        // Z-order as its rectangle and image background.
        float depth{};
        // Maps glyph pixel coordinates to final viewport pixel coordinates.
        // Visual2D supplies its hierarchical XYZ transform; ordinary text
        // submissions use the identity default.
        DirectX::XMFLOAT4X4 transform{
            1.0F, 0.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
            0.0F, 0.0F, 0.0F, 1.0F};
        TextHorizontalAlignment horizontalAlignment{
            TextHorizontalAlignment::Leading};
        TextVerticalAlignment verticalAlignment{
            TextVerticalAlignment::Near};
        // Pixel-space {left, top, right, bottom}. The default leaves text
        // unclipped inside the current render target.
        DirectX::XMFLOAT4 clipRectPixels{
            -1.0e9F, -1.0e9F, 1.0e9F, 1.0e9F};
        TextStyle style{};
    };

    // Text is submitted while a RenderContext is open. Submit performs
    // DirectWrite layout and records CPU-side glyph instances only; Flush
    // uploads cache misses and emits batched D3D12 draws at EndFrame.
    class TextRenderSystem final
    {
    public:
        static constexpr std::uint32_t FrameCount = 2;

        TextRenderSystem();
        ~TextRenderSystem();

        TextRenderSystem(const TextRenderSystem&) = delete;
        TextRenderSystem& operator=(const TextRenderSystem&) = delete;

        void Initialize(
            ID3D12Device& device,
            DXGI_FORMAT renderTargetFormat,
            DXGI_FORMAT depthStencilFormat = DXGI_FORMAT_UNKNOWN);
        void Shutdown() noexcept;

        [[nodiscard]] FontHandle LoadSystemFont(
            std::wstring_view familyName);
        [[nodiscard]] FontHandle LoadFontFile(
            const std::filesystem::path& fontFile);

        // renderIndex identifies one engine frame. Multiple BeginFrame/Flush
        // pairs with the same value are independent off-screen text passes
        // and append to the same fence-protected upload arena.
        void BeginFrame(
            std::uint32_t frameIndex,
            std::uint64_t renderIndex);
        void Submit(
            std::wstring_view text,
            const TextDrawCommand& command);
        void Flush(
            ID3D12GraphicsCommandList& commandList,
            std::uint32_t viewportWidth,
            std::uint32_t viewportHeight);

    private:
        struct Impl;
        std::unique_ptr<Impl> implementation_;
    };
}
