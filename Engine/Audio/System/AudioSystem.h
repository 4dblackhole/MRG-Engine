#pragma once

// Audio feature: backend-neutral system, device, and clip contracts.

#include <atomic>
#include <cstdint>
#include <filesystem>
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
        // Zero keeps the output driver's preferred mixer rate. Set an explicit
        // value only when the game needs a fixed software-mixer rate.
        int sampleRate{};
        std::uint32_t dspBufferLength{256};
        int dspBufferCount{4};
        bool fallBackToWasapi{true};
        bool allowNoSoundFallback{true};
        void* nativeWindowHandle{};
    };

    // A backend-specific clip owns one native sound object. The Client owns
    // the public AudioClip wrapper, while FMOD and other implementation types
    // remain outside the generated SDK header.
    class IAudioClipBackend
    {
    public:
        virtual ~IAudioClipBackend() = default;
        [[nodiscard]] virtual bool Play(std::string& errorMessage) = 0;
    };

    class AudioClip final
    {
    public:
        ~AudioClip();

        AudioClip(const AudioClip&) = delete;
        AudioClip& operator=(const AudioClip&) = delete;
        AudioClip(AudioClip&&) = delete;
        AudioClip& operator=(AudioClip&&) = delete;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] bool Play(std::string& errorMessage);

    private:
        friend class AudioSystem;

        AudioClip(
            std::unique_ptr<IAudioClipBackend> implementation,
            std::shared_ptr<std::atomic_size_t> liveClipCount);

        std::unique_ptr<IAudioClipBackend> implementation_;
        std::shared_ptr<std::atomic_size_t> liveClipCount_;
    };

    // The backend controls exactly one native audio system and its current
    // output. Driver enumeration is scoped to that current output and occurs
    // only after initialization or a successful output-API change.
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
        [[nodiscard]] virtual AudioOutputBackend RequestedOutput() const noexcept = 0;
        [[nodiscard]] virtual AudioOutputBackend ActiveOutput() const noexcept = 0;
        [[nodiscard]] virtual bool SetOutputBackend(
            AudioOutputBackend backend,
            std::string& errorMessage) = 0;

        [[nodiscard]] virtual int DriverCount() const noexcept = 0;
        [[nodiscard]] virtual const std::vector<AudioDeviceInfo>&
            OutputDrivers() const noexcept = 0;
        [[nodiscard]] virtual int ActiveDriverIndex() const noexcept = 0;
        [[nodiscard]] virtual bool SetOutputDriver(
            int driverIndex,
            std::string& errorMessage) = 0;

        [[nodiscard]] virtual int RequestedSampleRate() const noexcept = 0;
        [[nodiscard]] virtual int SampleRate() const noexcept = 0;
        [[nodiscard]] virtual std::uint32_t DspBufferLength() const noexcept = 0;
        [[nodiscard]] virtual int DspBufferCount() const noexcept = 0;
        [[nodiscard]] virtual double EstimatedDspLatencyMilliseconds() const noexcept = 0;
        [[nodiscard]] virtual bool SetSampleRate(
            int sampleRate,
            std::string& errorMessage) = 0;
        [[nodiscard]] virtual bool SetDspBufferSize(
            std::uint32_t bufferLength,
            int bufferCount,
            std::string& errorMessage) = 0;

        [[nodiscard]] virtual std::uint64_t DspClock() const noexcept = 0;
    };

    using AudioBackendFactory = std::unique_ptr<IAudioBackend> (*)();
    using AudioClipBackendFactory = std::unique_ptr<IAudioClipBackend> (*)(
        IAudioBackend& backend,
        const std::filesystem::path& path,
        std::string& errorMessage);

    [[nodiscard]] std::unique_ptr<IAudioBackend> CreateFmodAudioBackend();
    [[nodiscard]] std::unique_ptr<IAudioClipBackend>
        CreateFmodAudioClipBackend(
            IAudioBackend& backend,
            const std::filesystem::path& path,
            std::string& errorMessage);

    // Public backend-neutral service exposed to the rest of the engine. It
    // owns the system backend, but not Client AudioClip objects.
    class AudioSystem final
    {
    public:
        AudioSystem() = default;
        ~AudioSystem();

        AudioSystem(const AudioSystem&) = delete;
        AudioSystem& operator=(const AudioSystem&) = delete;

        [[nodiscard]] bool Initialize(
            const AudioConfig& config,
            AudioBackendFactory backendFactory,
            AudioClipBackendFactory clipFactory,
            std::string& errorMessage);
        void Update();
        void Shutdown() noexcept;

        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] std::string_view BackendName() const noexcept;
        [[nodiscard]] AudioOutputBackend RequestedOutput() const noexcept;
        [[nodiscard]] AudioOutputBackend ActiveOutput() const noexcept;
        [[nodiscard]] bool SetOutputBackend(
            AudioOutputBackend backend,
            std::string& errorMessage);

        [[nodiscard]] int DriverCount() const noexcept;
        [[nodiscard]] const std::vector<AudioDeviceInfo>&
            OutputDrivers() const noexcept;
        [[nodiscard]] int ActiveDriverIndex() const noexcept;
        [[nodiscard]] bool SetOutputDriver(
            int driverIndex,
            std::string& errorMessage);

        [[nodiscard]] int RequestedSampleRate() const noexcept;
        [[nodiscard]] int SampleRate() const noexcept;
        [[nodiscard]] std::uint32_t DspBufferLength() const noexcept;
        [[nodiscard]] int DspBufferCount() const noexcept;
        [[nodiscard]] double EstimatedDspLatencyMilliseconds() const noexcept;
        [[nodiscard]] bool SetSampleRate(
            int sampleRate,
            std::string& errorMessage);
        [[nodiscard]] bool SetDspBufferSize(
            std::uint32_t bufferLength,
            int bufferCount,
            std::string& errorMessage);

        [[nodiscard]] std::uint64_t DspClock() const noexcept;
        [[nodiscard]] std::unique_ptr<AudioClip> LoadSound(
            const std::filesystem::path& path,
            std::string& errorMessage);

    private:
        [[nodiscard]] bool InitializeExactBackend(
            const AudioConfig& config,
            AudioBackendFactory factory,
            std::string& errorMessage);
        [[nodiscard]] bool CanRestartMixer(std::string& errorMessage) const;

        std::unique_ptr<IAudioBackend> backend_;
        AudioClipBackendFactory clipFactory_{};
        AudioConfig config_{};
        std::shared_ptr<std::atomic_size_t> liveClipCount_;
        bool initialized_{};
    };
}
