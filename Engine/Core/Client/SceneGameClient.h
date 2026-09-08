#pragma once

#include "Client/IGameClient.h"
#include "Scene/SceneManager.h"
#include "System/AudioPlaybackManager.h"
#include "Visual2D/ScreenVisual2DManager.h"

#include <string_view>

namespace mrg::scene
{
    // Client contract adapter for games organized as switchable scenes.
    // Initialize creates the manager, calls RegisterScenes, then activates
    // InitialSceneId.  All later callbacks are forwarded to SceneManager.
    class SceneGameClient : public IGameClient
    {
    public:
        ~SceneGameClient() override;

        void Initialize(const EngineServices& services) final;
        [[nodiscard]] bool Update(const UpdateContext& context) final;
        void Render(const graphics::RenderContext& context) final;
        void OnResize(std::uint32_t width, std::uint32_t height) final;
        void Shutdown() noexcept final;

        // Shared by all Scenes in this Client; Scene transitions do not stop it.
        // Pass this service to Scene factories that need managed playback.
        [[nodiscard]] audio::AudioPlaybackManager& AudioPlayback() noexcept;
        [[nodiscard]] const audio::AudioPlaybackManager& AudioPlayback() const noexcept;

        // Owns screen Canvas trees and image registrations for this Client.
        // Pass it to Scene factories instead of submitting Canvas trees there.
        [[nodiscard]] visual2d::ScreenVisual2DManager& ScreenVisuals() noexcept;
        [[nodiscard]] const visual2d::ScreenVisual2DManager&
            ScreenVisuals() const noexcept;

    protected:
        virtual void RegisterScenes(SceneManager& scenes) = 0;
        [[nodiscard]] virtual std::string_view InitialSceneId() const noexcept = 0;

        [[nodiscard]] SceneManager& Scenes() noexcept;
        [[nodiscard]] const SceneManager& Scenes() const noexcept;

        // These hooks preserve SceneGameClient's ownership of SceneManager
        // while allowing a root Client to add game-wide behavior such as a
        // performance overlay after Scene rendering.
        virtual void OnClientInitialized(const EngineServices& services);
        virtual void OnClientUpdated(const UpdateContext& context);
        virtual void OnClientRendered(const graphics::RenderContext& context);
        virtual void OnClientShuttingDown() noexcept;

    private:
        // Declared before scenes_ so it also outlives Scene destructors.
        audio::AudioPlaybackManager audioPlayback_;
        visual2d::ScreenVisual2DManager screenVisuals_;
        SceneManager scenes_;
        bool initialized_{};
    };
}
