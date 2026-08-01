#pragma once

#include "Client/IGameClient.h"
#include "Scene/SceneManager.h"

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

    protected:
        virtual void RegisterScenes(SceneManager& scenes) = 0;
        [[nodiscard]] virtual std::string_view InitialSceneId() const noexcept = 0;

        [[nodiscard]] SceneManager& Scenes() noexcept;
        [[nodiscard]] const SceneManager& Scenes() const noexcept;

    private:
        SceneManager scenes_;
        bool initialized_{};
    };
}
