#pragma once

#include "Input/Input.h"
#include "Mesh/MeshRendering.h"
#include "Renderer/D3D12Renderer.h"
#include "System/AudioSystem.h"
#include "Text/TextRendering.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace mrg
{
    struct PerformanceOverlayConfig
    {
        // Relative paths are resolved from the executable directory. Empty
        // paths use the corresponding installed system-font family.
        std::filesystem::path framesPerSecondFontFile;
        std::filesystem::path updatesPerSecondFontFile;
        std::wstring framesPerSecondSystemFont{L"Consolas"};
        std::wstring updatesPerSecondSystemFont{L"Segoe UI"};
        float framesPerSecondFontSizePixels{18.0F};
        float updatesPerSecondFontSizePixels{22.0F};
        DirectX::XMFLOAT4 framesPerSecondColor{
            0.10F,
            0.40F,
            0.95F,
            1.0F};
        DirectX::XMFLOAT4 updatesPerSecondColor{
            0.95F,
            0.25F,
            0.12F,
            1.0F};
        float layoutWidthPixels{360.0F};
        float rightMarginPixels{20.0F};
        float bottomMarginPixels{18.0F};
        float lineGapPixels{4.0F};
        // Intended for automated hidden rendering checks. Interactive games
        // normally leave this false and use F1 to toggle the overlay.
        bool initiallyVisible{};
    };

    // Returned before any runtime subsystem exists. It lets the Client
    // describe the window, scheduling, and audio requirements to Run.
    struct EngineConfig
    {
        std::wstring windowTitle{L"My Rhythm Game"};
        std::uint32_t windowWidth{1280};
        std::uint32_t windowHeight{720};
        graphics::ClearColor clearColor{
            240.0F / 255.0F,
            248.0F / 255.0F,
            1.0F,
            1.0F};

        audio::AudioConfig audio{};
        audio::AudioBackendFactory audioBackendFactory{};
        audio::AudioClipBackendFactory audioClipBackendFactory{};
        double audioUpdateRateHz{500.0};
        double renderRateOverrideHz{};
        double maximumUpdateDeltaSeconds{0.1};
        PerformanceOverlayConfig performanceOverlay{};

        bool showWindow{true};
        std::uint64_t autoExitAfterRenderedFrames{};
    };

    // Non-owning services created by Run. They remain valid from Initialize
    // through the start of Shutdown; Client code must not release them or
    // retain them after Shutdown.
    struct EngineServices
    {
        graphics::MeshRenderSystem& meshRendering;
        graphics::TextRenderSystem& textRendering;
        audio::AudioSystem& audio;
        std::uint32_t windowWidth{};
        std::uint32_t windowHeight{};
    };

    // One Update invocation. deltaSeconds is QPC-derived and clamped by
    // EngineConfig. Input carries both current state and ordered Raw Input
    // events with their original QPC timestamps.
    struct UpdateContext
    {
        double deltaSeconds{};
        double totalSeconds{};
        std::uint64_t updateIndex{};
        const platform::InputState& input;
        audio::AudioSystem& audio;
    };

    // Contract implemented by a game-specific Client project.
    // Normal lifecycle: GetEngineConfig -> Initialize ->
    // [Update / Render / OnResize]* -> Shutdown.
    class IGameClient
    {
    public:
        virtual ~IGameClient();

        [[nodiscard]] virtual EngineConfig GetEngineConfig() const = 0;
        virtual void Initialize(const EngineServices& services) = 0;
        [[nodiscard]] virtual bool Update(const UpdateContext& context) = 0;
        virtual void Render(const graphics::RenderContext& context) = 0;
        virtual void OnResize(std::uint32_t width, std::uint32_t height) = 0;
        virtual void Shutdown() noexcept = 0;
    };
}
