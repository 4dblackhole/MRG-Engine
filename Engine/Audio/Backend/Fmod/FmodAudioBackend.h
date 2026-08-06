#pragma once

#include "System/AudioSystem.h"


#include <fmod.hpp>

#include <unordered_map>

namespace mrg::audio
{
    class FmodAudioBackend final : public IAudioBackend
    {
    public:
        ~FmodAudioBackend() override;

        [[nodiscard]] bool Initialize(
            const AudioConfig& config,
            std::string& errorMessage) override;
        void Update() override;
        void Shutdown() noexcept override;

        [[nodiscard]] std::string_view Name() const noexcept override;
        [[nodiscard]] AudioOutputBackend ActiveOutput() const noexcept override;
        [[nodiscard]] int SampleRate() const noexcept override;
        [[nodiscard]] std::uint64_t DspClock() const noexcept override;
        [[nodiscard]] const std::vector<AudioDeviceInfo>&
            OutputDevices() const noexcept override;
        [[nodiscard]] bool RefreshOutputDevices(
            std::string& errorMessage) override;
        [[nodiscard]] int ActiveDriverIndex() const noexcept override;
        [[nodiscard]] BackendSoundHandle LoadSound(
            const std::filesystem::path& path,
            std::string& errorMessage) override;
        [[nodiscard]] bool PlaySound(
            BackendSoundHandle sound,
            std::string& errorMessage) override;
        void UnloadSound(BackendSoundHandle sound) noexcept override;

    private:
        [[nodiscard]] bool TryInitialize(
            const AudioConfig& config,
            AudioOutputBackend backend,
            std::string& errorMessage);
        [[nodiscard]] bool EnumerateDevices(
            AudioOutputBackend backend,
            std::vector<AudioDeviceInfo>& destination,
            std::string& errorMessage);
        void RefreshDspState() noexcept;

        FMOD::System* system_{};
        FMOD::ChannelGroup* masterChannelGroup_{};
        AudioOutputBackend activeOutput_{AudioOutputBackend::NoSound};
        int sampleRate_{};
        int activeDriverIndex_{-1};
        BackendSoundHandle nextSoundHandle_{1};
        std::unordered_map<BackendSoundHandle, FMOD::Sound*> sounds_;
        std::vector<AudioDeviceInfo> outputDevices_;
    };
}
