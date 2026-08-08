#pragma once

#include "FmodAudioBackend.h"

#include <fmod.hpp>

#include <cstdint>
#include <memory>

namespace mrg::audio
{
    // Owns one FMOD sound for as long as the Client-owned AudioClip exists.
    class FmodAudioClip final : public IAudioClipBackend
    {
    public:
        FmodAudioClip(
            std::shared_ptr<FmodSystemLifetime> lifetime,
            FMOD::Sound* sound) noexcept;
        ~FmodAudioClip() override;

        [[nodiscard]] bool Play(std::string& errorMessage) override;

    private:
        [[nodiscard]] bool HasLiveSystem() const noexcept;

        std::shared_ptr<FmodSystemLifetime> lifetime_;
        FMOD::Sound* sound_{};
        std::uint64_t generation_{};
    };
}
