#include "WindowControlTests.h"
#include "MRG_Core.h"
#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool value, const char *message)
    {
        if (!value)
            throw std::runtime_error(message);
    }
    struct ResizeReport
    {
        bool resized{}, rendered{}, shutdown{};
    };
    class ResizeClient final : public mrg::IGameClient
    {
      public:
        explicit ResizeClient(ResizeReport &report) : report_(report)
        {
        }
        mrg::EngineConfig GetEngineConfig() const override
        {
            mrg::EngineConfig config;
            config.showWindow = false;
            config.audio.preferredBackend = mrg::audio::AudioOutputBackend::NoSound;
            return config;
        }
        void Initialize(const mrg::EngineServices &services) override
        {
            window_ = services.windowControl;
            Check(window_ != nullptr, "Run provides window control.");
            const auto initial = window_->ClientSize();
            std::string error;
            Check(!window_->SetClientSize({0, 720}, error) && !error.empty() && window_->ClientSize() == initial,
                  "Invalid resize preserves dimensions.");
            Check(!window_->SetClientSize({16385, 720}, error), "Reject dimensions exceeding the graphics limit.");
            const auto modes = window_->DisplayResolutions();
            Check(!modes.empty(), "Enumerate native display modes.");
            for (std::size_t i = 0; i < modes.size(); ++i)
                for (std::size_t j = i + 1; j < modes.size(); ++j)
                    Check(modes[i] != modes[j], "Deduplicate refresh rates.");
            Check(window_->SetClientSize({960, 540}, error) && error.empty(), "Native resize accepted.");
            Check(!report_.resized, "OnResize is deferred outside initialization.");
        }
        bool Update(const mrg::UpdateContext &context) override
        {
            Check(context.totalSeconds < 10, "Resize completes promptly.");
            return !report_.rendered;
        }
        void Render(const mrg::graphics::RenderContext &context) override
        {
            Check(report_.resized && window_->ClientSize() == mrg::platform::WindowResolution{960, 540},
                  "Renderer receives native size before rendering.");
            Check(context.width == 960 && context.height == 540, "Swap chain matches client size.");
            report_.rendered = true;
        }
        void OnResize(std::uint32_t width, std::uint32_t height) override
        {
            report_.resized = width == 960 && height == 540;
        }
        void Shutdown() noexcept override
        {
            report_.shutdown = true;
            window_ = nullptr;
        }

      private:
        ResizeReport &report_;
        mrg::platform::IWindowControl *window_{};
    };
} // namespace
int TestWindowControl()
{
    ResizeReport report;
    const auto result = mrg::Run(std::make_unique<ResizeClient>(report));
    if (result != 0 || !report.resized || !report.rendered || !report.shutdown)
        return 1;
    std::cout << "Native window resize, display modes and deferred renderer notification passed.\n";
    return 0;
}
