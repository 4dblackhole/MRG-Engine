#include "FmodAudioClip.h"

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

    bool FmodAudioClip::Play(std::string& errorMessage)
    {
        if (sound_ == nullptr || !HasLiveSystem())
        {
            errorMessage = "The FMOD sound is no longer valid.";
            return false;
        }

        const FMOD_RESULT result = lifetime_->system->playSound(
            sound_,
            nullptr,
            false,
            nullptr);
        if (result != FMOD_OK)
        {
            errorMessage = MakeFmodError("FMOD::System::playSound", result);
            return false;
        }
        errorMessage.clear();
        return true;
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
        std::string& errorMessage)
    {
        auto* fmodBackend = dynamic_cast<FmodAudioBackend*>(&backend);
        if (fmodBackend == nullptr ||
            fmodBackend->system_ == nullptr ||
            !fmodBackend->initialized_ ||
            fmodBackend->lifetime_ == nullptr)
        {
            errorMessage = "The FMOD backend is not initialized.";
            return nullptr;
        }

        FMOD::Sound* sound = nullptr;
        const std::string utf8Path = Utf8Path(path);
        const FMOD_RESULT result = fmodBackend->system_->createSound(
            utf8Path.c_str(),
            FMOD_DEFAULT | FMOD_CREATESAMPLE,
            nullptr,
            &sound);
        if (result != FMOD_OK || sound == nullptr)
        {
            errorMessage = MakeFmodError("FMOD::System::createSound", result);
            return nullptr;
        }

        errorMessage.clear();
        return std::make_unique<FmodAudioClip>(
            fmodBackend->lifetime_,
            sound);
    }
}
