#include "FmodPcmClip.h"
#include "FmodAudioClip.h"
#include "Decode/AudioPcmDecoder.h"

#include <fmod_errors.h>
#include <chrono>
#include <future>
#include <limits>
#include <stdexcept>
#include <stop_token>
#include <vector>

namespace mrg::audio
{
    namespace
    {
        struct SourcePcm
        {
            std::uint32_t channels{}, rate{};
            std::vector<float> samples;
        };

        SourcePcm ReadSource(const std::filesystem::path& path, const std::stop_token stop)
        {
            SourcePcm pcm;
            DecodeAudioPcm(path, stop, [&](const std::uint32_t channels, const std::uint32_t rate,
                                           const std::span<const float> samples)
            {
                if (channels == 0 || channels > 32 || rate == 0 ||
                    rate > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
                    samples.size() % channels != 0 ||
                    (pcm.rate != 0 && (pcm.channels != channels || pcm.rate != rate)))
                    throw std::runtime_error("PCM source has an invalid or changing format.");
                pcm.channels = channels;
                pcm.rate = rate;
                constexpr auto maxSamples = std::numeric_limits<unsigned int>::max() / sizeof(float);
                if (samples.size() > maxSamples - pcm.samples.size())
                    throw std::runtime_error("PCM source exceeds the backend's byte-length range.");
                pcm.samples.insert(pcm.samples.end(), samples.begin(), samples.end());
            });
            if (stop.stop_requested())
                throw std::runtime_error("PCM source loading was cancelled.");
            if (pcm.samples.empty())
                throw std::runtime_error("PCM source contains no audio frames.");
            return pcm;
        }

        bool IsAsync(const AudioLoadMode mode)
        {
            return mode == AudioLoadMode::StreamAsync || mode == AudioLoadMode::StreamAsyncAccurate ||
                   mode == AudioLoadMode::SampleAsync;
        }

        // The public clip retains the decoded source for raw-memory streams.
        // FMOD owns only playback/mixing; it never parses this compressed file.
        class FmodPcmClip final : public IAudioClipBackend
        {
        public:
            FmodPcmClip(std::shared_ptr<FmodSystemLifetime> lifetime,
                        const std::filesystem::path& path, const AudioLoadMode mode)
                : lifetime_(std::move(lifetime)), generation_(lifetime_->generation), mode_(mode),
                  pending_(std::async(IsAsync(mode) ? std::launch::async : std::launch::deferred,
                                      [path, stop = cancellation_.get_token()] { return ReadSource(path, stop); }))
            {
            }
            ~FmodPcmClip() override { cancellation_.request_stop(); }

            AudioClipLoadState LoadState(std::string& error) const override
            {
                if (!failure_.empty())
                {
                    error = failure_;
                    return AudioClipLoadState::Failed;
                }
                if (!lifetime_ || !lifetime_->system || lifetime_->generation != generation_)
                {
                    error = "The PCM clip no longer belongs to a live audio system.";
                    return AudioClipLoadState::Failed;
                }
                if (!clip_)
                {
                    const auto state = pending_.wait_for(std::chrono::seconds{0});
                    if (state == std::future_status::timeout)
                    {
                        error.clear();
                        return AudioClipLoadState::Loading;
                    }
                    try
                    {
                        pcm_ = pending_.get();
                        OpenPcm();
                    }
                    catch (const std::exception& exception)
                    {
                        failure_ = exception.what();
                        error = failure_;
                        return AudioClipLoadState::Failed;
                    }
                }
                return clip_->LoadState(error);
            }

            bool GetDurationMicroseconds(std::uint64_t& duration, std::string& error) const override
            {
                duration = 0;
                const auto state = LoadState(error);
                if (state == AudioClipLoadState::Loading)
                {
                    error = "The PCM source is still loading.";
                    return false;
                }
                return state != AudioClipLoadState::Failed && clip_->GetDurationMicroseconds(duration, error);
            }

            std::unique_ptr<IAudioVoiceBackend> Play(const AudioPlaybackSettings& settings,
                                                    IAudioBusBackend* bus, std::string& error) override
            {
                const auto state = LoadState(error);
                if (state == AudioClipLoadState::Loading)
                    error = "The PCM source is still loading.";
                if (state != AudioClipLoadState::Ready && state != AudioClipLoadState::Playing)
                    return nullptr;
                return clip_->Play(settings, bus, error);
            }

        private:
            void OpenPcm() const
            {
                FMOD_CREATESOUNDEXINFO info{};
                info.cbsize = sizeof(info);
                info.length = static_cast<unsigned int>(pcm_.samples.size() * sizeof(float));
                info.numchannels = static_cast<int>(pcm_.channels);
                info.defaultfrequency = static_cast<int>(pcm_.rate);
                info.format = FMOD_SOUND_FORMAT_PCMFLOAT;
                const bool sample = mode_ == AudioLoadMode::Sample || mode_ == AudioLoadMode::SampleAsync;
                const FMOD_MODE flags = FMOD_OPENMEMORY | FMOD_OPENRAW |
                                        (sample ? FMOD_CREATESAMPLE : FMOD_CREATESTREAM) |
                                        (IsAsync(mode_) ? FMOD_NONBLOCKING : 0);
                FMOD::Sound* sound{};
                const auto result = lifetime_->system->createSound(
                    reinterpret_cast<const char*>(pcm_.samples.data()), flags, &info, &sound);
                if (result != FMOD_OK || !sound)
                    throw std::runtime_error(std::string("FMOD PCM source creation failed: ") +
                                             FMOD_ErrorString(result));
                // Keep the native resource under RAII if allocating its wrapper fails.
                try { clip_ = std::make_unique<FmodAudioClip>(lifetime_, sound); }
                catch (...) { static_cast<void>(sound->release()); throw; }
            }

            std::shared_ptr<FmodSystemLifetime> lifetime_;
            std::uint64_t generation_{};
            AudioLoadMode mode_;
            std::stop_source cancellation_;
            mutable std::future<SourcePcm> pending_;
            mutable SourcePcm pcm_;
            mutable std::unique_ptr<FmodAudioClip> clip_;
            mutable std::string failure_;
        };
    }

    std::unique_ptr<IAudioClipBackend> CreateFmodPcmClip(
        FmodAudioBackend& backend, const std::filesystem::path& path,
        const AudioLoadMode mode, std::string& error)
    {
        try
        {
            auto result = std::make_unique<FmodPcmClip>(backend.Lifetime(), path, mode);
            if (!IsAsync(mode) && result->LoadState(error) == AudioClipLoadState::Failed)
                return nullptr;
            error.clear();
            return result;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return nullptr;
        }
    }
}
