#include "HeadlessRunTests.h"
#include "MRG_Core.h"
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace
{
    class FailingHiddenClient final : public mrg::IGameClient
    {
      public:
        explicit FailingHiddenClient(bool &shutdown) : shutdown_(shutdown)
        {
        }
        mrg::EngineConfig GetEngineConfig() const override
        {
            mrg::EngineConfig config;
            config.showWindow = false;
            config.audio.preferredBackend = mrg::audio::AudioOutputBackend::NoSound;
            return config;
        }
        void Initialize(const mrg::EngineServices &) override
        {
        }
        bool Update(const mrg::UpdateContext &) override
        {
            throw std::runtime_error("Expected hidden Update failure.");
        }
        void Render(const mrg::graphics::RenderContext &) override
        {
        }
        void OnResize(std::uint32_t, std::uint32_t) override
        {
        }
        void Shutdown() noexcept override
        {
            shutdown_ = true;
        }

      private:
        bool &shutdown_;
    };
} // namespace

int TestHeadlessRunError()
{
    bool shutdown = false;
    const auto result = mrg::Run(std::make_unique<FailingHiddenClient>(shutdown));
    if (result != EXIT_FAILURE || !shutdown)
    {
        std::cerr << "Hidden Run must fail without blocking and clean up the initialized Client.\n";
        return EXIT_FAILURE;
    }
    std::cout << "Hidden fatal-error cleanup passed without a modal dialog.\n";
    return EXIT_SUCCESS;
}
