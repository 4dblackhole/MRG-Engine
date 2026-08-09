#pragma once

// Backend-neutral playback, mixer-bus, and DSP-effect contracts.

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace mrg::audio
{
    enum class AudioLoadMode : std::uint8_t
    {
        Sample,
        Stream,
    };

    enum class AudioEffectType : std::uint8_t
    {
        LowPass,
        HighPass,
        Compressor,
        Delay,
        Reverb,
    };

    // A compact, backend-neutral parameter vocabulary. Parameters that do
    // not apply to an effect are rejected by the backend with an error.
    enum class AudioEffectParameter : std::uint8_t
    {
        CutoffHz,
        Resonance,
        ThresholdDb,
        Ratio,
        AttackMilliseconds,
        ReleaseMilliseconds,
        DelayMilliseconds,
        DecayTimeMilliseconds,
        EarlyDelayMilliseconds,
        LateDelayMilliseconds,
        WetLevelDb,
        DryLevelDb,
        DiffusionPercent,
        DensityPercent,
    };

    struct AudioPlaybackSettings
    {
        float volume{1.0F};
        float pitch{1.0F};
        bool startPaused{};
        // Zero means immediate playback. Non-zero values use the same DSP
        // clock domain returned by AudioSystem::CaptureClockSnapshot().
        std::uint64_t startDspClock{};
        std::uint64_t endDspClock{};
    };

    struct AudioClockSnapshot
    {
        std::uint64_t dspClock{};
        int sampleRate{};
        std::int64_t performanceCounterTicks{};
        std::int64_t performanceCounterFrequency{};
    };

    class IAudioVoiceBackend
    {
    public:
        virtual ~IAudioVoiceBackend() = default;
        [[nodiscard]] virtual bool IsPlaying() const noexcept = 0;
        [[nodiscard]] virtual bool Stop(std::string& errorMessage) = 0;
        [[nodiscard]] virtual bool SetPaused(
            bool paused,
            std::string& errorMessage) = 0;
        [[nodiscard]] virtual bool SetVolume(
            float volume,
            std::string& errorMessage) = 0;
        [[nodiscard]] virtual bool SetPitch(
            float pitch,
            std::string& errorMessage) = 0;
    };

    class IAudioEffectBackend
    {
    public:
        virtual ~IAudioEffectBackend() = default;
        [[nodiscard]] virtual AudioEffectType Type() const noexcept = 0;
        [[nodiscard]] virtual bool SetBypass(
            bool bypass,
            std::string& errorMessage) = 0;
        [[nodiscard]] virtual bool SetParameter(
            AudioEffectParameter parameter,
            float value,
            std::string& errorMessage) = 0;
    };

    class IAudioBusBackend
    {
    public:
        virtual ~IAudioBusBackend() = default;
        [[nodiscard]] virtual std::string_view Name() const noexcept = 0;
        [[nodiscard]] virtual bool SetVolume(
            float volume,
            std::string& errorMessage) = 0;
        [[nodiscard]] virtual bool SetPitch(
            float pitch,
            std::string& errorMessage) = 0;
        [[nodiscard]] virtual bool SetMuted(
            bool muted,
            std::string& errorMessage) = 0;
        [[nodiscard]] virtual bool AddFadePoint(
            std::uint64_t dspClock,
            float volume,
            bool ramp,
            std::string& errorMessage) = 0;
        [[nodiscard]] virtual bool ClearFadePoints(
            std::uint64_t beginDspClock,
            std::uint64_t endDspClock,
            std::string& errorMessage) = 0;
        [[nodiscard]] virtual std::unique_ptr<IAudioEffectBackend> AddEffect(
            AudioEffectType type,
            std::string& errorMessage) = 0;
    };

    class AudioVoice final
    {
    public:
        ~AudioVoice();

        AudioVoice(const AudioVoice&) = delete;
        AudioVoice& operator=(const AudioVoice&) = delete;
        AudioVoice(AudioVoice&&) = delete;
        AudioVoice& operator=(AudioVoice&&) = delete;

        [[nodiscard]] bool IsPlaying() const noexcept;
        [[nodiscard]] bool Stop(std::string& errorMessage);
        [[nodiscard]] bool SetPaused(bool paused, std::string& errorMessage);
        [[nodiscard]] bool SetVolume(float volume, std::string& errorMessage);
        [[nodiscard]] bool SetPitch(float pitch, std::string& errorMessage);

    private:
        friend class AudioClip;

        AudioVoice(
            std::unique_ptr<IAudioVoiceBackend> implementation,
            std::shared_ptr<std::atomic_size_t> liveObjectCount);

        std::unique_ptr<IAudioVoiceBackend> implementation_;
        std::shared_ptr<std::atomic_size_t> liveObjectCount_;
    };

    class AudioEffect final
    {
    public:
        ~AudioEffect();

        AudioEffect(const AudioEffect&) = delete;
        AudioEffect& operator=(const AudioEffect&) = delete;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] AudioEffectType Type() const noexcept;
        [[nodiscard]] bool SetBypass(bool bypass, std::string& errorMessage);
        [[nodiscard]] bool SetParameter(
            AudioEffectParameter parameter,
            float value,
            std::string& errorMessage);

    private:
        friend class AudioBus;

        AudioEffect(
            std::unique_ptr<IAudioEffectBackend> implementation,
            std::shared_ptr<std::atomic_size_t> liveObjectCount);

        std::unique_ptr<IAudioEffectBackend> implementation_;
        std::shared_ptr<std::atomic_size_t> liveObjectCount_;
    };

    class AudioBus final
    {
    public:
        ~AudioBus();

        AudioBus(const AudioBus&) = delete;
        AudioBus& operator=(const AudioBus&) = delete;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] std::string_view Name() const noexcept;
        [[nodiscard]] bool SetVolume(float volume, std::string& errorMessage);
        [[nodiscard]] bool SetPitch(float pitch, std::string& errorMessage);
        [[nodiscard]] bool SetMuted(bool muted, std::string& errorMessage);
        [[nodiscard]] bool AddFadePoint(
            std::uint64_t dspClock,
            float volume,
            bool ramp,
            std::string& errorMessage);
        [[nodiscard]] bool ClearFadePoints(
            std::uint64_t beginDspClock,
            std::uint64_t endDspClock,
            std::string& errorMessage);
        [[nodiscard]] std::unique_ptr<AudioEffect> AddEffect(
            AudioEffectType type,
            std::string& errorMessage);

    private:
        friend class AudioClip;
        friend class AudioSystem;

        AudioBus(
            std::unique_ptr<IAudioBusBackend> implementation,
            std::shared_ptr<std::atomic_size_t> liveObjectCount);

        std::unique_ptr<IAudioBusBackend> implementation_;
        std::shared_ptr<std::atomic_size_t> liveObjectCount_;
    };
}
