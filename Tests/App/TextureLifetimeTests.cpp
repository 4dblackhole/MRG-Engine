#include "TextureLifetimeTests.h"
#include "PhantomCore.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
    void Require(bool value, const char *message)
    {
        if (!value)
            throw std::runtime_error(message);
    }
    struct Report
    {
        unsigned frames{};
        bool shutdown{};
    };
    class TextureLifetimeClient final : public IGameClient
    {
      public:
        explicit TextureLifetimeClient(Report &report) : report_(report)
        {
        }
        EngineConfig GetEngineConfig() const override
        {
            EngineConfig config;
            config.showWindow = false;
            config.audio.preferredBackend = AudioOutputBackend::NoSound;
            return config;
        }
        void Initialize(const EngineServices &services) override
        {
            textures_ = &services.meshRendering.Textures();
            rendering_ = &services.visual2DRendering;
            for (int i = 0; i < 200; ++i)
                static_cast<void>(textures_->CreateRenderTargetTexture(1, 1));
            std::vector<RenderTargetTextureHandle> live;
            for (int i = 0; i < 16; ++i)
                live.push_back(textures_->CreateRenderTargetTexture(1, 1));
            bool refused = false;
            try
            {
                static_cast<void>(textures_->CreateRenderTargetTexture(1, 1));
            }
            catch (const std::runtime_error &)
            {
                refused = true;
            }
            Require(refused, "Live texture sets must never share a descriptor block.");
            live.clear();
            static_cast<void>(textures_->CreateRenderTargetTexture(1, 1));
            const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
            imagePath_ =
                std::filesystem::temp_directory_path() / ("phantomcore-texture-lifetime-" + std::to_string(nonce) + ".bmp");
            constexpr std::array<unsigned char, 58> image{0x42, 0x4d, 58, 0, 0, 0, 0, 0, 0, 0,    54,   0,    0, 0,  40,
                                                          0,    0,    0,  1, 0, 0, 0, 1, 0, 0,    0,    1,    0, 24, 0,
                                                          0,    0,    0,  0, 4, 0, 0, 0, 0, 0,    0,    0,    0, 0,  0,
                                                          0,    0,    0,  0, 0, 0, 0, 0, 0, 0xff, 0xff, 0xff, 0};
            std::ofstream file(imagePath_, std::ios::binary);
            file.exceptions(std::ios::badbit | std::ios::failbit);
            file.write(reinterpret_cast<const char *>(image.data()), image.size());
            file.close();
            const std::array paths{imagePath_};
            auto upload = textures_->LoadTextureSet(paths);
            firstUpload_ = upload;
            upload.reset();
            Require(!firstUpload_.expired(), "A submitted upload must retain its descriptor lease until its fence.");
        }
        bool Update(const UpdateContext &context) override
        {
            Require(context.totalSeconds < 15, "Texture lifetime regression timed out.");
            return report_.frames < 64;
        }
        void Render(const RenderContext &context) override
        {
            if (context.frameIndex >= targets_.size())
                throw std::runtime_error("Unexpected frame-ring size.");
            const auto previous = targets_[context.frameIndex];
            auto target = textures_->CreateRenderTargetTexture(16, 16);
            rendering_->RenderToTexture(canvas_, target, context);
            Require(previous.expired(), "A reused frame slot must release its completed target.");
            targets_[context.frameIndex] = target;
            const auto address = textures_->GpuDescriptorStart(target->Textures()).ptr;
            target.reset();
            Require(!targets_[context.frameIndex].expired(), "Recorded GPU work must retain its target.");
            auto another = textures_->CreateRenderTargetTexture(1, 1);
            Require(textures_->GpuDescriptorStart(another->Textures()).ptr != address,
                    "An in-flight target descriptor cannot be reassigned.");
            ++report_.frames;
            if (report_.frames >= 4)
                Require(firstUpload_.expired(), "Completed upload leases must become reusable.");
            const std::array paths{imagePath_};
            static_cast<void>(textures_->LoadTextureSet(paths));
        }
        void OnResize(std::uint32_t, std::uint32_t) override
        {
        }
        void Shutdown() noexcept override
        {
            std::error_code ignored;
            std::filesystem::remove(imagePath_, ignored);
            report_.shutdown = true;
        }

      private:
        Report &report_;
        TextureManager *textures_{};
        Visual2DRenderSystem *rendering_{};
        Visual2DCanvas canvas_;
        std::filesystem::path imagePath_;
        std::weak_ptr<const TextureSet> firstUpload_;
        std::array<std::weak_ptr<RenderTargetTexture>, 16> targets_;
    };
} // namespace
int TestTextureLifetime()
{
    Report report;
    const auto exitCode = Run(std::make_unique<TextureLifetimeClient>(report));
    if (exitCode || report.frames != 64 || !report.shutdown)
        return 1;
    std::cout << "Descriptor reuse: 200 released targets, live-capacity limit "
                 "and 64 GPU frames passed.\n";
    return 0;
}
