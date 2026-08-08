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
            OnClientInitialized(services);
            initialized_ = true;
        }
        catch (...)
        {
            // A derived Client may have created root-level resources in its
            // initialization hook before a later step throws.
            OnClientShuttingDown();
            scenes_.Shutdown();
            throw;
        }
    }

    bool SceneGameClient::Update(const UpdateContext& context)
    {
        if (!initialized_)
        {
            return false;
        }

        const bool keepRunning = scenes_.Update(context);
        OnClientUpdated(context);
        return keepRunning;
    }

    void SceneGameClient::Render(
        const graphics::RenderContext& context)
    {
        if (initialized_)
        {
            scenes_.Render(context);
            OnClientRendered(context);
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
        OnClientShuttingDown();
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

    void SceneGameClient::OnClientInitialized(const EngineServices&)
    {
    }

    void SceneGameClient::OnClientUpdated(const UpdateContext&)
    {
    }

    void SceneGameClient::OnClientRendered(const graphics::RenderContext&)
    {
    }

    void SceneGameClient::OnClientShuttingDown() noexcept
    {
    }
}
