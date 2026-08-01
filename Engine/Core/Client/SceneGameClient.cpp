#include "Client/SceneGameClient.h"

#include <stdexcept>

namespace mrg::scene
{
    SceneGameClient::~SceneGameClient() = default;

    void SceneGameClient::Initialize(const EngineServices& services)
    {
        if (initialized_)
        {
            throw std::logic_error("SceneGameClient is already initialized.");
        }

        scenes_.Initialize(services);
        try
        {
            RegisterScenes(scenes_);
            if (!scenes_.Start(InitialSceneId()))
            {
                throw std::runtime_error(
                    "The initial game scene is not registered.");
            }
            initialized_ = true;
        }
        catch (...)
        {
            scenes_.Shutdown();
            throw;
        }
    }

    bool SceneGameClient::Update(const UpdateContext& context)
    {
        return initialized_ && scenes_.Update(context);
    }

    void SceneGameClient::Render(
        const graphics::RenderContext& context)
    {
        if (initialized_)
        {
            scenes_.Render(context);
        }
    }

    void SceneGameClient::OnResize(
        const std::uint32_t width,
        const std::uint32_t height)
    {
        if (initialized_)
        {
            scenes_.OnResize(width, height);
        }
    }

    void SceneGameClient::Shutdown() noexcept
    {
        scenes_.Shutdown();
        initialized_ = false;
    }

    SceneManager& SceneGameClient::Scenes() noexcept
    {
        return scenes_;
    }

    const SceneManager& SceneGameClient::Scenes() const noexcept
    {
        return scenes_;
    }
}
