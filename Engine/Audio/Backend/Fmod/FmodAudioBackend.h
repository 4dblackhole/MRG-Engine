#pragma once

#include "System/AudioSystem.h"


#include <fmod.hpp>

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

    private:
        [[nodiscard]] bool TryInitialize(
            const AudioConfig& config,
            AudioOutputBackend backend,
            std::string& errorMessage);
        void EnumerateDevices(AudioOutputBackend backend);
        void RefreshDspState() noexcept;

        FMOD::System* system_{};
        FMOD::ChannelGroup* masterChannelGroup_{};
        AudioOutputBackend activeOutput_{AudioOutputBackend::NoSound};
        int sampleRate_{};
        std::vector<AudioDeviceInfo> outputDevices_;
    };
}
