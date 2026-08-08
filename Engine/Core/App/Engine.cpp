#include "App/Engine.h"

#include "System/ComApartment.h"
#include "System/HighResolutionClock.h"
#include "System/RuntimePaths.h"
#include "Window/Win32Window.h"

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace mrg
{
    namespace
    {
        double ValidRate(const double rate, const double fallback) noexcept
        {
            return rate > 1.0 ? rate : fallback;
        }

        void AdvanceDeadline(
            double& deadline,
            const double interval,
            const double now) noexcept
        {
            deadline += interval;
            if (deadline < now - interval)
            {
                deadline = now + interval;
            }
        }

        struct PerformanceValues final
        {
            std::uint64_t framesPerSecond{};
            std::uint64_t updatesPerSecond{};
        };

        [[nodiscard]] PerformanceValues CalculatePerformanceValues(
            const std::uint64_t renderedFrameCount,
            const std::uint64_t updateCount,
            const double elapsedSeconds)
        {
            return {
                static_cast<std::uint64_t>(
                    static_cast<double>(renderedFrameCount) /
                        elapsedSeconds +
                    0.5),
                static_cast<std::uint64_t>(
                    static_cast<double>(updateCount) /
                        elapsedSeconds +
                    0.5)};
        }

        [[nodiscard]] graphics::FontHandle LoadOverlayFont(
            graphics::TextRenderSystem& textRendering,
            const std::filesystem::path& fontFile,
            const std::wstring_view systemFont)
        {
            if (!fontFile.empty())
            {
                return textRendering.LoadFontFile(
                    platform::ResolveExecutableRelativePath(fontFile));
            }
            return textRendering.LoadSystemFont(systemFont);
        }

        void SubmitPerformanceOverlay(
            graphics::TextRenderSystem& textRendering,
            const PerformanceOverlayConfig& config,
            const graphics::FontHandle& framesPerSecondFont,
            const graphics::FontHandle& updatesPerSecondFont,
            const std::wstring_view framesPerSecondText,
            const std::wstring_view updatesPerSecondText,
            const std::uint32_t viewportWidth,
            const std::uint32_t viewportHeight)
        {
            const float width = static_cast<float>(viewportWidth);
            const float height = static_cast<float>(viewportHeight);
            const float availableWidth = std::max(
                width - config.rightMarginPixels,
                1.0F);
            const float layoutWidth = std::clamp(
                config.layoutWidthPixels,
                1.0F,
                availableWidth);
            const float layoutX = std::max(
                availableWidth - layoutWidth,
                0.0F);
            const float framesLineHeight = std::max(
                config.framesPerSecondFontSizePixels * 1.5F,
                1.0F);
            const float updatesLineHeight = std::max(
                config.updatesPerSecondFontSizePixels * 1.5F,
                1.0F);
            const float updatesY = std::max(
                height - config.bottomMarginPixels - updatesLineHeight,
                0.0F);
            const float framesY = std::max(
                updatesY - config.lineGapPixels - framesLineHeight,
                0.0F);

            const auto submitLine =
                [&textRendering, layoutX, layoutWidth](
                    const std::wstring_view text,
                    const graphics::FontHandle& font,
                    const float fontSizePixels,
                    const DirectX::XMFLOAT4& color,
                    const float y,
                    const float lineHeight)
            {
                graphics::TextDrawCommand command;
                command.positionPixels = {layoutX, y};
                command.layoutSizePixels = {layoutWidth, lineHeight};
                command.horizontalAlignment =
                    graphics::TextHorizontalAlignment::Trailing;
                command.verticalAlignment =
                    graphics::TextVerticalAlignment::Center;
                command.style.font = font;
                command.style.fontSizePixels = fontSizePixels;

                // A small shadow keeps both configured colors legible over
                // bright and textured Client content.
                command.positionPixels.x += 1.5F;
                command.positionPixels.y += 1.5F;
                command.style.color = {0.0F, 0.0F, 0.0F, 0.65F};
                textRendering.Submit(text, command);

                command.positionPixels.x -= 1.5F;
                command.positionPixels.y -= 1.5F;
                command.style.color = color;
                textRendering.Submit(text, command);
            };

            submitLine(
                framesPerSecondText,
                framesPerSecondFont,
                config.framesPerSecondFontSizePixels,
                config.framesPerSecondColor,
                framesY,
                framesLineHeight);
            submitLine(
                updatesPerSecondText,
                updatesPerSecondFont,
                config.updatesPerSecondFontSizePixels,
                config.updatesPerSecondColor,
                updatesY,
                updatesLineHeight);
        }

        struct MainLoopState final
        {
            MainLoopState(
                const EngineConfig& config,
                const double refreshRateHz)
                : showPerformanceStatistics(
                      config.performanceOverlay.initiallyVisible),
                  renderInterval(
                      1.0 / ValidRate(
                          config.renderRateOverrideHz,
                          refreshRateHz)),
                  audioInterval(
                      1.0 / ValidRate(config.audioUpdateRateHz, 500.0))
            {
            }

            void ResetPerformanceMeasurement() noexcept
            {
                updatesSinceStatisticsReport = 0;
                rendersSinceStatisticsReport = 0;
                statisticsReportStartSeconds = totalSeconds;
                framesPerSecondText = L"FPS: measuring...";
                updatesPerSecondText = L"UPS: measuring...";
            }

            system::HighResolutionClock clock;
            double totalSeconds{};
            std::uint64_t updateIndex{};
            std::uint64_t renderedFrames{};
            std::uint64_t updatesSinceStatisticsReport{};
            std::uint64_t rendersSinceStatisticsReport{};
            double statisticsReportStartSeconds{};
            bool showPerformanceStatistics{};
            std::wstring framesPerSecondText{L"FPS: measuring..."};
            std::wstring updatesPerSecondText{L"UPS: measuring..."};
            double renderInterval{};
            double nextRenderTime{};
            double audioInterval{};
            double nextAudioUpdateTime{};
        };

        void HandleEngineCommands(
            const platform::InputState& input,
            MainLoopState& state)
        {
            if (!input.WasKeyPressed(VK_F1))
            {
                return;
            }

            state.showPerformanceStatistics =
                !state.showPerformanceStatistics;
            state.ResetPerformanceMeasurement();
        }

        void HandleWindowChanges(
            platform::Win32Window& window,
            graphics::D3D12Renderer& renderer,
            IGameClient& client,
            const EngineConfig& config,
            MainLoopState& state)
        {
            platform::WindowSize newSize{};
            if (window.ConsumeResize(newSize))
            {
                renderer.Resize(newSize.width, newSize.height);
                client.OnResize(newSize.width, newSize.height);
            }

            double newRefreshRate = 0.0;
            if (window.ConsumeRefreshRateChange(newRefreshRate) &&
                config.renderRateOverrideHz <= 1.0)
            {
                state.renderInterval =
                    1.0 / ValidRate(newRefreshRate, 60.0);
                state.nextRenderTime = state.totalSeconds;
            }
        }

        [[nodiscard]] bool UpdateClient(
            IGameClient& client,
            const platform::InputState& input,
            audio::AudioSystem& audioSystem,
            const EngineConfig& config,
            const double rawDeltaSeconds,
            MainLoopState& state)
        {
            const UpdateContext updateContext{
                std::clamp(
                    rawDeltaSeconds,
                    0.0,
                    config.maximumUpdateDeltaSeconds),
                state.totalSeconds,
                state.updateIndex++,
                input,
                audioSystem};

            // Update is intentionally unthrottled. Rendering and FMOD use
            // independent deadlines and never sleep this loop.
            const bool keepRunning = client.Update(updateContext);
            if (state.showPerformanceStatistics)
            {
                ++state.updatesSinceStatisticsReport;
            }
            return keepRunning;
        }

        void ServiceAudio(
            audio::AudioSystem& audioSystem,
            MainLoopState& state)
        {
            if (state.totalSeconds < state.nextAudioUpdateTime)
            {
                return;
            }

            audioSystem.Update();
            AdvanceDeadline(
                state.nextAudioUpdateTime,
                state.audioInterval,
                state.totalSeconds);
        }

        [[nodiscard]] bool RenderIfDue(
            IGameClient& client,
            const platform::Win32Window& window,
            graphics::D3D12Renderer& renderer,
            const EngineConfig& config,
            const graphics::FontHandle& framesPerSecondFont,
            const graphics::FontHandle& updatesPerSecondFont,
            MainLoopState& state)
        {
            if (window.IsMinimized() ||
                state.totalSeconds < state.nextRenderTime)
            {
                return true;
            }

            // BeginFrame waits only when this back buffer is still in flight;
            // Client::Render records work into the opened command list.
            const graphics::RenderContext renderContext =
                renderer.BeginFrame(config.clearColor);
            client.Render(renderContext);
            if (state.showPerformanceStatistics)
            {
                SubmitPerformanceOverlay(
                    renderer.TextRendering(),
                    config.performanceOverlay,
                    framesPerSecondFont,
                    updatesPerSecondFont,
                    state.framesPerSecondText,
                    state.updatesPerSecondText,
                    renderContext.width,
                    renderContext.height);
            }
            renderer.EndFrame();

            ++state.renderedFrames;
            if (state.showPerformanceStatistics)
            {
                ++state.rendersSinceStatisticsReport;
            }
            AdvanceDeadline(
                state.nextRenderTime,
                state.renderInterval,
                state.totalSeconds);

            return config.autoExitAfterRenderedFrames == 0 ||
                state.renderedFrames < config.autoExitAfterRenderedFrames;
        }

        void RefreshPerformanceText(MainLoopState& state)
        {
            const double elapsedSeconds =
                state.totalSeconds - state.statisticsReportStartSeconds;
            if (!state.showPerformanceStatistics || elapsedSeconds < 1.0)
            {
                return;
            }

            const PerformanceValues values = CalculatePerformanceValues(
                state.rendersSinceStatisticsReport,
                state.updatesSinceStatisticsReport,
                elapsedSeconds);
            state.framesPerSecondText =
                L"FPS: " + std::to_wstring(values.framesPerSecond);
            state.updatesPerSecondText =
                L"UPS: " + std::to_wstring(values.updatesPerSecond);
            state.updatesSinceStatisticsReport = 0;
            state.rendersSinceStatisticsReport = 0;
            state.statisticsReportStartSeconds = state.totalSeconds;
        }

        void RunMainLoop(
            IGameClient& client,
            platform::InputState& input,
            platform::Win32Window& window,
            graphics::D3D12Renderer& renderer,
            audio::AudioSystem& audioSystem,
            const EngineConfig& config,
            const graphics::FontHandle& framesPerSecondFont,
            const graphics::FontHandle& updatesPerSecondFont)
        {
            MainLoopState state(config, window.RefreshRateHz());
            bool running = true;
            while (running && window.PumpMessages())
            {
                // PumpMessages resets transient state and dispatches every
                // queued Raw Input message before the Client update.
                const double rawDeltaSeconds =
                    state.clock.Tick(state.totalSeconds);
                HandleEngineCommands(input, state);
                HandleWindowChanges(window, renderer, client, config, state);
                running = UpdateClient(
                    client,
                    input,
                    audioSystem,
                    config,
                    rawDeltaSeconds,
                    state);
                ServiceAudio(audioSystem, state);
                if (running)
                {
                    running = RenderIfDue(
                        client,
                        window,
                        renderer,
                        config,
                        framesPerSecondFont,
                        updatesPerSecondFont,
                        state);
                }
                RefreshPerformanceText(state);
            }
        }
    }

    int Run(std::unique_ptr<IGameClient> client)
    {
        if (client == nullptr)
        {
            return EXIT_FAILURE;
        }

        bool clientInitialized = false;

        try
        {
            // The Win32 main thread owns the message pump and must remain an
            // STA so COM-based ASIO drivers can be created by FMOD.
            platform::ComApartment comApartment(
                COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            EngineConfig config = client->GetEngineConfig();

            // Stack order is intentional.  The Client is initialized only
            // after the native window, renderer, and audio service exist.
            platform::InputState input;
            platform::Win32Window window;
            window.Initialize(
                platform::WindowConfig{
                    config.windowTitle,
                    config.windowWidth,
                    config.windowHeight,
                    config.showWindow},
                input);

            graphics::D3D12Renderer renderer;
            renderer.Initialize(
                window.Handle(),
                window.ClientWidth(),
                window.ClientHeight());

            const graphics::FontHandle framesPerSecondFont =
                LoadOverlayFont(
                    renderer.TextRendering(),
                    config.performanceOverlay.framesPerSecondFontFile,
                    config.performanceOverlay.framesPerSecondSystemFont);
            const graphics::FontHandle updatesPerSecondFont =
                LoadOverlayFont(
                    renderer.TextRendering(),
                    config.performanceOverlay.updatesPerSecondFontFile,
                    config.performanceOverlay.updatesPerSecondSystemFont);

            audio::AudioSystem audioSystem;
            config.audio.nativeWindowHandle = window.Handle();
            std::string audioError;
            if (!audioSystem.Initialize(
                    config.audio,
                    config.audioBackendFactory,
                    config.audioClipBackendFactory,
                    audioError))
            {
                throw std::runtime_error(
                    "Audio initialization failed: " + audioError);
            }

            const EngineServices services{
                renderer.MeshRendering(),
                renderer.TextRendering(),
                renderer.Visual2DRendering(),
                audioSystem,
                window.ClientWidth(),
                window.ClientHeight()};
            // Client resource creation uses the high-level mesh/material
            // service; root signatures and PSOs remain owned by Graphics.
            client->Initialize(services);
            clientInitialized = true;

            RunMainLoop(
                *client,
                input,
                window,
                renderer,
                audioSystem,
                config,
                framesPerSecondFont,
                updatesPerSecondFont);

            // Client scenes own D3D12 resources.  First ensure no submitted
            // frame refers to them, then release Client resources while the
            // renderer/device are still alive.
            renderer.WaitForGpu();
            client->Shutdown();
            clientInitialized = false;
            audioSystem.Shutdown();
            return EXIT_SUCCESS;
        }
        catch (const std::exception& exception)
        {
            if (clientInitialized)
            {
                // Keep the same Client-before-renderer destruction order on
                // failure.  Stack RAII then shuts down the remaining systems.
                client->Shutdown();
            }
            MessageBoxA(
                nullptr,
                exception.what(),
                "MyRhythmGame fatal error",
                MB_OK | MB_ICONERROR);
            return EXIT_FAILURE;
        }
    }
}
