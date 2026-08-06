#pragma once

// Audio feature: backend-neutral public service and backend contract.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mrg::audio
{
    using AudioSoundHandle = std::uint64_t;
    using BackendSoundHandle = std::uint64_t;
    inline constexpr AudioSoundHandle InvalidAudioSoundHandle = 0;
    inline constexpr BackendSoundHandle InvalidBackendSoundHandle = 0;

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
        // Rebuilds the backend's output-device snapshot. Backends without
        // dynamic enumeration may keep their current list and return true.
        [[nodiscard]] virtual bool RefreshOutputDevices(
            std::string& errorMessage)
        {
            errorMessage.clear();
            return true;
        }
        [[nodiscard]] virtual int ActiveDriverIndex() const noexcept = 0;
        [[nodiscard]] virtual BackendSoundHandle LoadSound(
            const std::filesystem::path& path,
            std::string& errorMessage) = 0;
        [[nodiscard]] virtual bool PlaySound(
            BackendSoundHandle sound,
            std::string& errorMessage) = 0;
        virtual void UnloadSound(BackendSoundHandle sound) noexcept = 0;
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
        // Refresh before presenting a device picker so drivers installed or
        // connected after engine startup can become visible.
        [[nodiscard]] bool RefreshOutputDevices(std::string& errorMessage);
        [[nodiscard]] int ActiveDriverIndex() const noexcept;

        // Initializes a replacement backend first and commits the device
        // change only after every registered sound has been recreated. A
        // failed switch therefore leaves the currently active output intact.
        [[nodiscard]] bool SelectOutputDevice(
            const AudioDeviceInfo& device,
            std::string& errorMessage);
        [[nodiscard]] AudioSoundHandle LoadSound(
            const std::filesystem::path& path,
            std::string& errorMessage);
        [[nodiscard]] bool PlaySound(
            AudioSoundHandle sound,
            std::string& errorMessage);
        void UnloadSound(AudioSoundHandle sound) noexcept;

    private:
        struct RegisteredSound
        {
            std::filesystem::path path;
            BackendSoundHandle backendHandle{InvalidBackendSoundHandle};
        };

        [[nodiscard]] std::unique_ptr<IAudioBackend> CreateBackend() const;

        std::unique_ptr<IAudioBackend> backend_;
        std::unordered_map<AudioSoundHandle, RegisteredSound> sounds_;
        AudioConfig config_{};
        AudioBackendFactory factory_{};
        AudioSoundHandle nextSoundHandle_{1};
        bool initialized_{};
    };
}
