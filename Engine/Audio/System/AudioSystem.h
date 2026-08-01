#pragma once

// Audio feature: backend-neutral public service and backend contract.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace mrg::audio
{
    enum class AudioOutputBackend
    {
        Automatic,
        Wasapi,
        Asio,
        NoSound,
    };

    struct AudioDeviceInfo
    {
        AudioOutputBackend backend{AudioOutputBackend::Automatic};
        int driverIndex{-1};
        std::string name;
        int sampleRate{};
        int speakerChannels{};
    };

    struct AudioConfig
    {
        AudioOutputBackend preferredBackend{AudioOutputBackend::Automatic};
        int driverIndex{-1};
        int maxVirtualChannels{256};
        std::uint32_t dspBufferLength{256};
        int dspBufferCount{4};
        bool fallBackToWasapi{true};
        bool allowNoSoundFallback{true};
        void* nativeWindowHandle{};
    };

    class IAudioBackend
    {
    public:
        virtual ~IAudioBackend() = default;

        [[nodiscard]] virtual bool Initialize(
            const AudioConfig& config,
            std::string& errorMessage) = 0;
        virtual void Update() = 0;
        virtual void Shutdown() noexcept = 0;

        [[nodiscard]] virtual std::string_view Name() const noexcept = 0;
        [[nodiscard]] virtual AudioOutputBackend ActiveOutput() const noexcept = 0;
        [[nodiscard]] virtual int SampleRate() const noexcept = 0;
        [[nodiscard]] virtual std::uint64_t DspClock() const noexcept = 0;
        [[nodiscard]] virtual const std::vector<AudioDeviceInfo>&
            OutputDevices() const noexcept = 0;
    };

    using AudioBackendFactory = std::unique_ptr<IAudioBackend> (*)();

    [[nodiscard]] std::unique_ptr<IAudioBackend> CreateFmodAudioBackend();

    // Public backend-neutral audio service exposed to the rest of the engine.
    // Run creates it before Client initialization and calls Update at the
    // independently configured audio cadence.  Client code only sees this
    // contract, so the FMOD backend can later be replaced without API changes.
    class AudioSystem final
    {
    public:
        AudioSystem() = default;
        ~AudioSystem();

        AudioSystem(const AudioSystem&) = delete;
        AudioSystem& operator=(const AudioSystem&) = delete;

        [[nodiscard]] bool Initialize(
            const AudioConfig& config,
            AudioBackendFactory factory,
            std::string& errorMessage);
        void Update();
        void Shutdown() noexcept;

        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] std::string_view BackendName() const noexcept;
        [[nodiscard]] AudioOutputBackend ActiveOutput() const noexcept;
        [[nodiscard]] int SampleRate() const noexcept;
        [[nodiscard]] std::uint64_t DspClock() const noexcept;
        [[nodiscard]] const std::vector<AudioDeviceInfo>&
            OutputDevices() const noexcept;

    private:
        std::unique_ptr<IAudioBackend> backend_;
        bool initialized_{};
    };
}
