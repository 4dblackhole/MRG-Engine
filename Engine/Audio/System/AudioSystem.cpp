#include "System/AudioSystem.h"

#include "Backend/Fmod/FmodAudioBackend.h"

#include <limits>
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
        factory_ = factory != nullptr ? factory : &CreateFmodAudioBackend;
        config_ = config;
        backend_ = CreateBackend();

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
            factory_ = nullptr;
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
        sounds_.clear();
        if (backend_ != nullptr)
        {
            backend_->Shutdown();
            backend_.reset();
        }
        initialized_ = false;
        factory_ = nullptr;
        nextSoundHandle_ = 1;
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

    int AudioSystem::ActiveDriverIndex() const noexcept
    {
        return backend_ != nullptr ? backend_->ActiveDriverIndex() : -1;
    }

    bool AudioSystem::SelectOutputDevice(
        const AudioDeviceInfo& device,
        std::string& errorMessage)
    {
        if (!initialized_ || backend_ == nullptr)
        {
            errorMessage = "The audio system is not initialized.";
            return false;
        }
        const bool automaticDefault =
            device.backend == AudioOutputBackend::Automatic &&
            device.driverIndex == -1;
        const bool explicitDriver = device.driverIndex >= 0 &&
            (device.backend == AudioOutputBackend::Wasapi ||
                device.backend == AudioOutputBackend::Asio);
        if (!automaticDefault && !explicitDriver)
        {
            errorMessage = "The selected audio output device is invalid.";
            return false;
        }
        if (device.backend == backend_->ActiveOutput() &&
            device.driverIndex == backend_->ActiveDriverIndex())
        {
            errorMessage.clear();
            return true;
        }

        AudioConfig replacementConfig = config_;
        replacementConfig.preferredBackend = device.backend;
        replacementConfig.driverIndex = device.driverIndex;
        // An explicit UI choice must report failure rather than silently
        // selecting a different output path.
        replacementConfig.fallBackToWasapi = false;
        replacementConfig.allowNoSoundFallback = false;

        std::unique_ptr<IAudioBackend> replacement = CreateBackend();
        if (replacement == nullptr)
        {
            errorMessage = "The audio backend factory returned null.";
            return false;
        }
        if (!replacement->Initialize(replacementConfig, errorMessage))
        {
            replacement->Shutdown();
            return false;
        }

        std::unordered_map<AudioSoundHandle, BackendSoundHandle>
            replacementHandles;
        replacementHandles.reserve(sounds_.size());
        for (const auto& [handle, sound] : sounds_)
        {
            const BackendSoundHandle replacementSound =
                replacement->LoadSound(sound.path, errorMessage);
            if (replacementSound == InvalidBackendSoundHandle)
            {
                replacement->Shutdown();
                return false;
            }
            replacementHandles.emplace(handle, replacementSound);
        }

        backend_->Shutdown();
        backend_ = std::move(replacement);
        for (auto& [handle, sound] : sounds_)
        {
            sound.backendHandle = replacementHandles.at(handle);
        }
        config_ = replacementConfig;
        errorMessage.clear();
        return true;
    }

    AudioSoundHandle AudioSystem::LoadSound(
        const std::filesystem::path& path,
        std::string& errorMessage)
    {
        if (!initialized_ || backend_ == nullptr)
        {
            errorMessage = "The audio system is not initialized.";
            return InvalidAudioSoundHandle;
        }
        if (nextSoundHandle_ == InvalidAudioSoundHandle ||
            nextSoundHandle_ == std::numeric_limits<AudioSoundHandle>::max())
        {
            errorMessage = "The audio sound handle space is exhausted.";
            return InvalidAudioSoundHandle;
        }

        const BackendSoundHandle backendHandle =
            backend_->LoadSound(path, errorMessage);
        if (backendHandle == InvalidBackendSoundHandle)
        {
            return InvalidAudioSoundHandle;
        }

        const AudioSoundHandle result = nextSoundHandle_++;
        sounds_.emplace(result, RegisteredSound{path, backendHandle});
        errorMessage.clear();
        return result;
    }

    bool AudioSystem::PlaySound(
        const AudioSoundHandle sound,
        std::string& errorMessage)
    {
        if (!initialized_ || backend_ == nullptr)
        {
            errorMessage = "The audio system is not initialized.";
            return false;
        }
        const auto found = sounds_.find(sound);
        if (found == sounds_.end())
        {
            errorMessage = "The audio sound handle is invalid.";
            return false;
        }
        return backend_->PlaySound(found->second.backendHandle, errorMessage);
    }

    void AudioSystem::UnloadSound(const AudioSoundHandle sound) noexcept
    {
        const auto found = sounds_.find(sound);
        if (found == sounds_.end())
        {
            return;
        }
        if (backend_ != nullptr)
        {
            backend_->UnloadSound(found->second.backendHandle);
        }
        sounds_.erase(found);
    }

    std::unique_ptr<IAudioBackend> AudioSystem::CreateBackend() const
    {
        return factory_ != nullptr ? factory_() : nullptr;
    }
}
