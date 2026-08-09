#include "FmodAudioClip.h"

#include "FmodAudioBus.h"

#include <fmod_errors.h>

#include <filesystem>
#include <string>
#include <utility>

namespace mrg::audio
{
    namespace
    {
        std::string MakeFmodError(
            const char* operation,
            const FMOD_RESULT result)
        {
            return std::string(operation) + " failed: " +
                FMOD_ErrorString(result) + " (" +
                std::to_string(static_cast<int>(result)) + ")";
        }

        [[nodiscard]] std::string Utf8Path(
            const std::filesystem::path& path)
        {
            const std::u8string value = path.u8string();
            return std::string(
                reinterpret_cast<const char*>(value.data()),
                value.size());
        }

        class FmodAudioVoice final : public IAudioVoiceBackend
        {
        public:
            FmodAudioVoice(
                std::shared_ptr<FmodSystemLifetime> lifetime,
                FMOD::Channel* const channel) noexcept
                : lifetime_(std::move(lifetime)),
                  channel_(channel),
                  generation_(
                      lifetime_ != nullptr ? lifetime_->generation : 0)
            {
            }

            [[nodiscard]] bool IsPlaying() const noexcept override
            {
                if (!HasLiveSystem())
                {
                    return false;
                }
                bool isPlaying = false;
                return channel_->isPlaying(&isPlaying) == FMOD_OK && isPlaying;
            }

            [[nodiscard]] bool Stop(
                std::string& errorMessage) override
            {
                return Apply(
                    "FMOD::Channel::stop",
                    HasLiveSystem() ? channel_->stop() : FMOD_ERR_INVALID_HANDLE,
                    errorMessage);
            }

            [[nodiscard]] bool SetPaused(
                const bool paused,
                std::string& errorMessage) override
            {
                return Apply(
                    "FMOD::Channel::setPaused",
                    HasLiveSystem()
                        ? channel_->setPaused(paused)
                        : FMOD_ERR_INVALID_HANDLE,
                    errorMessage);
            }

            [[nodiscard]] bool SetVolume(
                const float volume,
                std::string& errorMessage) override
            {
                return Apply(
                    "FMOD::Channel::setVolume",
                    HasLiveSystem()
                        ? channel_->setVolume(volume)
                        : FMOD_ERR_INVALID_HANDLE,
                    errorMessage);
            }

            [[nodiscard]] bool SetPitch(
                const float pitch,
                std::string& errorMessage) override
            {
                return Apply(
                    "FMOD::Channel::setPitch",
                    HasLiveSystem()
                        ? channel_->setPitch(pitch)
                        : FMOD_ERR_INVALID_HANDLE,
                    errorMessage);
            }

        private:
            [[nodiscard]] bool HasLiveSystem() const noexcept
            {
                return channel_ != nullptr &&
                    lifetime_ != nullptr &&
                    lifetime_->system != nullptr &&
                    lifetime_->generation == generation_;
            }

            [[nodiscard]] static bool Apply(
                const char* operation,
                const FMOD_RESULT result,
                std::string& errorMessage)
            {
                if (result != FMOD_OK)
                {
                    errorMessage = MakeFmodError(operation, result);
                    return false;
                }
                errorMessage.clear();
                return true;
            }

            std::shared_ptr<FmodSystemLifetime> lifetime_;
            FMOD::Channel* channel_{};
            std::uint64_t generation_{};
        };
    }

    FmodAudioClip::FmodAudioClip(
        std::shared_ptr<FmodSystemLifetime> lifetime,
        FMOD::Sound* sound) noexcept
        : lifetime_(std::move(lifetime)),
          sound_(sound),
          generation_(lifetime_ != nullptr ? lifetime_->generation : 0)
    {
    }

    FmodAudioClip::~FmodAudioClip()
    {
        // System::close already invalidates and frees its child objects. Only
        // call release while this sound still belongs to the live generation.
        if (sound_ != nullptr && HasLiveSystem())
        {
            static_cast<void>(sound_->release());
        }
        sound_ = nullptr;
    }

    std::unique_ptr<IAudioVoiceBackend> FmodAudioClip::Play(
        const AudioPlaybackSettings& settings,
        IAudioBusBackend* const bus,
        std::string& errorMessage)
    {
        if (sound_ == nullptr || !HasLiveSystem())
        {
            errorMessage = "The FMOD sound is no longer valid.";
            return nullptr;
        }

        FMOD::ChannelGroup* nativeBus = nullptr;
        if (bus != nullptr)
        {
            auto* const fmodBus = dynamic_cast<FmodAudioBus*>(bus);
            if (fmodBus == nullptr || !fmodBus->HasLiveSystem())
            {
                errorMessage =
                    "The audio bus belongs to another backend or is invalid.";
                return nullptr;
            }
            nativeBus = fmodBus->NativeGroup();
        }

        FMOD::Channel* channel = nullptr;
        FMOD_RESULT result = lifetime_->system->playSound(
            sound_,
            nativeBus,
            true,
            &channel);
        if (result != FMOD_OK || channel == nullptr)
        {
            errorMessage = MakeFmodError("FMOD::System::playSound", result);
            return nullptr;
        }

        const auto failPlayback = [&](
            const char* operation,
            const FMOD_RESULT failure)
        {
            static_cast<void>(channel->stop());
            errorMessage = MakeFmodError(operation, failure);
            return std::unique_ptr<IAudioVoiceBackend>{};
        };

        result = channel->setVolume(settings.volume);
        if (result != FMOD_OK)
        {
            return failPlayback("FMOD::Channel::setVolume", result);
        }
        result = channel->setPitch(settings.pitch);
        if (result != FMOD_OK)
        {
            return failPlayback("FMOD::Channel::setPitch", result);
        }
        if (settings.startDspClock != 0 || settings.endDspClock != 0)
        {
            result = channel->setDelay(
                settings.startDspClock,
                settings.endDspClock,
                settings.endDspClock != 0);
            if (result != FMOD_OK)
            {
                return failPlayback("FMOD::Channel::setDelay", result);
            }
        }
        result = channel->setPaused(settings.startPaused);
        if (result != FMOD_OK)
        {
            return failPlayback("FMOD::Channel::setPaused", result);
        }

        errorMessage.clear();
        return std::make_unique<FmodAudioVoice>(lifetime_, channel);
    }

    bool FmodAudioClip::HasLiveSystem() const noexcept
    {
        return lifetime_ != nullptr &&
            lifetime_->system != nullptr &&
            lifetime_->generation == generation_;
    }

    std::unique_ptr<IAudioClipBackend> CreateFmodAudioClipBackend(
        IAudioBackend& backend,
        const std::filesystem::path& path,
        const AudioLoadMode loadMode,
        std::string& errorMessage)
    {
        auto* fmodBackend = dynamic_cast<FmodAudioBackend*>(&backend);
        if (fmodBackend == nullptr ||
            fmodBackend->NativeSystem() == nullptr ||
            !fmodBackend->IsMixerInitialized() ||
            fmodBackend->Lifetime() == nullptr)
        {
            errorMessage = "The FMOD backend is not initialized.";
            return nullptr;
        }

        FMOD::Sound* sound = nullptr;
        const std::string utf8Path = Utf8Path(path);
        const FMOD_MODE mode = FMOD_DEFAULT |
            (loadMode == AudioLoadMode::Stream
                ? FMOD_CREATESTREAM
                : FMOD_CREATESAMPLE);
        const FMOD_RESULT result = fmodBackend->NativeSystem()->createSound(
            utf8Path.c_str(),
            mode,
            nullptr,
            &sound);
        if (result != FMOD_OK || sound == nullptr)
        {
            errorMessage = MakeFmodError("FMOD::System::createSound", result);
            return nullptr;
        }

        errorMessage.clear();
        return std::make_unique<FmodAudioClip>(
            fmodBackend->Lifetime(),
            sound);
    }
}
