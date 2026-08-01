#include "System/AudioSystem.h"

#include "Backend/Fmod/FmodAudioBackend.h"

#include <utility>

namespace mrg::audio
{
    // The default factory is kept here so FMOD stays out of the public API.
    std::unique_ptr<IAudioBackend> CreateFmodAudioBackend()
    {
        return std::make_unique<FmodAudioBackend>();
    }

    AudioSystem::~AudioSystem()
    {
        Shutdown();
    }

    bool AudioSystem::Initialize(
        const AudioConfig& config,
        const AudioBackendFactory factory,
        std::string& errorMessage)
    {
        Shutdown();
        // A Client may inject another backend factory; the default remains
        // private so FMOD types never leak into the public audio contract.
        backend_ = factory != nullptr
            ? factory()
            : CreateFmodAudioBackend();

        if (backend_ == nullptr)
        {
            errorMessage = "The audio backend factory returned null.";
            return false;
        }

        initialized_ = backend_->Initialize(config, errorMessage);
        if (!initialized_)
        {
            backend_->Shutdown();
            backend_.reset();
        }
        return initialized_;
    }

    void AudioSystem::Update()
    {
        if (initialized_)
        {
            backend_->Update();
        }
    }

    void AudioSystem::Shutdown() noexcept
    {
        if (backend_ != nullptr)
        {
            backend_->Shutdown();
            backend_.reset();
        }
        initialized_ = false;
    }

    bool AudioSystem::IsInitialized() const noexcept
    {
        return initialized_;
    }

    std::string_view AudioSystem::BackendName() const noexcept
    {
        return backend_ != nullptr ? backend_->Name() : "None";
    }

    AudioOutputBackend AudioSystem::ActiveOutput() const noexcept
    {
        return backend_ != nullptr
            ? backend_->ActiveOutput()
            : AudioOutputBackend::NoSound;
    }

    int AudioSystem::SampleRate() const noexcept
    {
        return backend_ != nullptr ? backend_->SampleRate() : 0;
    }

    std::uint64_t AudioSystem::DspClock() const noexcept
    {
        return backend_ != nullptr ? backend_->DspClock() : 0;
    }

    const std::vector<AudioDeviceInfo>& AudioSystem::OutputDevices() const noexcept
    {
        static const std::vector<AudioDeviceInfo> empty;
        return backend_ != nullptr ? backend_->OutputDevices() : empty;
    }
}
