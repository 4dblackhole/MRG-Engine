#include "FmodAudioBackend.h"


#include <fmod_errors.h>

#include <Windows.h>

#include <array>
#include <filesystem>
#include <string>
#include <utility>

namespace mrg::audio
{
    namespace
    {
        FMOD_OUTPUTTYPE ToFmodOutput(const AudioOutputBackend backend) noexcept
        {
            switch (backend)
            {
            case AudioOutputBackend::Wasapi:
                return FMOD_OUTPUTTYPE_WASAPI;
            case AudioOutputBackend::Asio:
                return FMOD_OUTPUTTYPE_ASIO;
            case AudioOutputBackend::NoSound:
                return FMOD_OUTPUTTYPE_NOSOUND;
            case AudioOutputBackend::Automatic:
            default:
                return FMOD_OUTPUTTYPE_AUTODETECT;
            }
        }

        AudioOutputBackend FromFmodOutput(const FMOD_OUTPUTTYPE output) noexcept
        {
            switch (output)
            {
            case FMOD_OUTPUTTYPE_WASAPI:
                return AudioOutputBackend::Wasapi;
            case FMOD_OUTPUTTYPE_ASIO:
                return AudioOutputBackend::Asio;
            case FMOD_OUTPUTTYPE_NOSOUND:
                return AudioOutputBackend::NoSound;
            default:
                return AudioOutputBackend::Automatic;
            }
        }

        std::string MakeFmodError(
            const char* operation,
            const FMOD_RESULT result)
        {
            return std::string(operation) + " failed: " +
                FMOD_ErrorString(result) + " (" +
                std::to_string(static_cast<int>(result)) + ")";
        }

        void DebugLog(const std::string& message)
        {
            OutputDebugStringA(("[MRG.Audio] " + message + "\n").c_str());
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

    FmodAudioBackend::~FmodAudioBackend()
    {
        Shutdown();
    }

    bool FmodAudioBackend::Initialize(
        const AudioConfig& config,
        std::string& errorMessage)
    {
        Shutdown();
        outputDevices_.clear();
        EnumerateDevices(AudioOutputBackend::Wasapi);
        EnumerateDevices(AudioOutputBackend::Asio);

        if (TryInitialize(config, config.preferredBackend, errorMessage))
        {
            return true;
        }

        DebugLog(errorMessage);

        if (config.preferredBackend == AudioOutputBackend::Asio &&
            config.fallBackToWasapi &&
            TryInitialize(config, AudioOutputBackend::Wasapi, errorMessage))
        {
            DebugLog("ASIO was unavailable; WASAPI was selected.");
            return true;
        }

        if (config.allowNoSoundFallback &&
            TryInitialize(config, AudioOutputBackend::NoSound, errorMessage))
        {
            DebugLog("No audio output was available; FMOD no-sound mode was selected.");
            return true;
        }

        return false;
    }

    void FmodAudioBackend::Update()
    {
        if (system_ == nullptr)
        {
            return;
        }

        const FMOD_RESULT result = system_->update();
        if (result != FMOD_OK)
        {
            DebugLog(MakeFmodError("FMOD::System::update", result));
        }
    }

    void FmodAudioBackend::Shutdown() noexcept
    {
        for (auto& [handle, sound] : sounds_)
        {
            static_cast<void>(handle);
            if (sound != nullptr)
            {
                sound->release();
            }
        }
        sounds_.clear();
        masterChannelGroup_ = nullptr;
        if (system_ != nullptr)
        {
            system_->close();
            system_->release();
            system_ = nullptr;
        }

        activeOutput_ = AudioOutputBackend::NoSound;
        sampleRate_ = 0;
        activeDriverIndex_ = -1;
        nextSoundHandle_ = 1;
    }

    std::string_view FmodAudioBackend::Name() const noexcept
    {
        return "FMOD Core";
    }

    AudioOutputBackend FmodAudioBackend::ActiveOutput() const noexcept
    {
        return activeOutput_;
    }

    int FmodAudioBackend::SampleRate() const noexcept
    {
        return sampleRate_;
    }

    std::uint64_t FmodAudioBackend::DspClock() const noexcept
    {
        if (masterChannelGroup_ == nullptr)
        {
            return 0;
        }

        unsigned long long dspClock = 0;
        unsigned long long parentClock = 0;
        if (masterChannelGroup_->getDSPClock(&dspClock, &parentClock) != FMOD_OK)
        {
            return 0;
        }
        return static_cast<std::uint64_t>(dspClock);
    }

    const std::vector<AudioDeviceInfo>&
    FmodAudioBackend::OutputDevices() const noexcept
    {
        return outputDevices_;
    }

    int FmodAudioBackend::ActiveDriverIndex() const noexcept
    {
        return activeDriverIndex_;
    }

    BackendSoundHandle FmodAudioBackend::LoadSound(
        const std::filesystem::path& path,
        std::string& errorMessage)
    {
        if (system_ == nullptr)
        {
            errorMessage = "FMOD is not initialized.";
            return InvalidBackendSoundHandle;
        }

        FMOD::Sound* sound = nullptr;
        const std::string utf8Path = Utf8Path(path);
        const FMOD_RESULT result = system_->createSound(
            utf8Path.c_str(),
            FMOD_DEFAULT | FMOD_CREATESAMPLE,
            nullptr,
            &sound);
        if (result != FMOD_OK || sound == nullptr)
        {
            errorMessage = MakeFmodError("FMOD::System::createSound", result);
            return InvalidBackendSoundHandle;
        }

        const BackendSoundHandle handle = nextSoundHandle_++;
        sounds_.emplace(handle, sound);
        errorMessage.clear();
        return handle;
    }

    bool FmodAudioBackend::PlaySound(
        const BackendSoundHandle sound,
        std::string& errorMessage)
    {
        if (system_ == nullptr)
        {
            errorMessage = "FMOD is not initialized.";
            return false;
        }
        const auto found = sounds_.find(sound);
        if (found == sounds_.end())
        {
            errorMessage = "The FMOD sound handle is invalid.";
            return false;
        }

        const FMOD_RESULT result = system_->playSound(
            found->second,
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

    void FmodAudioBackend::UnloadSound(
        const BackendSoundHandle sound) noexcept
    {
        const auto found = sounds_.find(sound);
        if (found == sounds_.end())
        {
            return;
        }
        if (found->second != nullptr)
        {
            found->second->release();
        }
        sounds_.erase(found);
    }

    bool FmodAudioBackend::TryInitialize(
        const AudioConfig& config,
        const AudioOutputBackend backend,
        std::string& errorMessage)
    {
        Shutdown();

        // Phase 1: create a fresh FMOD system and reject a runtime DLL that
        // is older than the headers used to compile the engine.
        FMOD_RESULT result = FMOD::System_Create(&system_);
        if (result != FMOD_OK)
        {
            errorMessage = MakeFmodError("FMOD::System_Create", result);
            system_ = nullptr;
            return false;
        }

        unsigned int runtimeVersion = 0;
        result = system_->getVersion(&runtimeVersion);
        if (result != FMOD_OK || runtimeVersion < FMOD_VERSION)
        {
            errorMessage = result != FMOD_OK
                ? MakeFmodError("FMOD::System::getVersion", result)
                : "The FMOD runtime DLL is older than the FMOD headers.";
            Shutdown();
            return false;
        }

        // Phase 2: select the requested output path and configure buffering
        // before FMOD::System::init, as required by the Core API.
        const FMOD_OUTPUTTYPE requestedOutput = ToFmodOutput(backend);
        if (requestedOutput != FMOD_OUTPUTTYPE_AUTODETECT)
        {
            result = system_->setOutput(requestedOutput);
            if (result != FMOD_OK)
            {
                errorMessage = MakeFmodError("FMOD::System::setOutput", result);
                Shutdown();
                return false;
            }
        }

        result = system_->setDSPBufferSize(
            config.dspBufferLength,
            config.dspBufferCount);
        if (result != FMOD_OK)
        {
            errorMessage =
                MakeFmodError("FMOD::System::setDSPBufferSize", result);
            Shutdown();
            return false;
        }

        // Phase 3: validate and select a concrete driver when the Client did
        // not request FMOD's default device.
        if (config.driverIndex >= 0 &&
            backend != AudioOutputBackend::NoSound)
        {
            int driverCount = 0;
            result = system_->getNumDrivers(&driverCount);
            if (result != FMOD_OK ||
                config.driverIndex >= driverCount)
            {
                errorMessage = "The selected FMOD output driver index is invalid.";
                Shutdown();
                return false;
            }

            result = system_->setDriver(config.driverIndex);
            if (result != FMOD_OK)
            {
                errorMessage = MakeFmodError("FMOD::System::setDriver", result);
                Shutdown();
                return false;
            }
        }

        // Phase 4: initialize the mixer, then cache the actual backend and
        // master DSP information reported by FMOD.
        void* extraDriverData = backend == AudioOutputBackend::Asio
            ? config.nativeWindowHandle
            : nullptr;
        result = system_->init(
            config.maxVirtualChannels,
            FMOD_INIT_NORMAL,
            extraDriverData);
        if (result != FMOD_OK)
        {
            errorMessage = MakeFmodError("FMOD::System::init", result);
            Shutdown();
            return false;
        }

        FMOD_OUTPUTTYPE actualOutput = FMOD_OUTPUTTYPE_UNKNOWN;
        if (system_->getOutput(&actualOutput) == FMOD_OK)
        {
            activeOutput_ = FromFmodOutput(actualOutput);
        }
        else
        {
            activeOutput_ = backend;
        }

        RefreshDspState();
        if (system_->getDriver(&activeDriverIndex_) != FMOD_OK)
        {
            activeDriverIndex_ = config.driverIndex;
        }
        errorMessage.clear();
        return true;
    }

    void FmodAudioBackend::EnumerateDevices(const AudioOutputBackend backend)
    {
        FMOD::System* probe = nullptr;
        if (FMOD::System_Create(&probe) != FMOD_OK || probe == nullptr)
        {
            return;
        }

        const FMOD_OUTPUTTYPE output = ToFmodOutput(backend);
        if (probe->setOutput(output) != FMOD_OK)
        {
            probe->release();
            return;
        }

        int driverCount = 0;
        if (probe->getNumDrivers(&driverCount) != FMOD_OK)
        {
            probe->release();
            return;
        }

        for (int driverIndex = 0; driverIndex < driverCount; ++driverIndex)
        {
            std::array<char, 512> name{};
            FMOD_GUID guid{};
            int systemRate = 0;
            FMOD_SPEAKERMODE speakerMode = FMOD_SPEAKERMODE_DEFAULT;
            int speakerChannels = 0;

            if (probe->getDriverInfo(
                    driverIndex,
                    name.data(),
                    static_cast<int>(name.size()),
                    &guid,
                    &systemRate,
                    &speakerMode,
                    &speakerChannels) != FMOD_OK)
            {
                continue;
            }

            outputDevices_.push_back(AudioDeviceInfo{
                backend,
                driverIndex,
                name.data(),
                systemRate,
                speakerChannels});
        }

        probe->release();
    }

    void FmodAudioBackend::RefreshDspState() noexcept
    {
        FMOD_SPEAKERMODE speakerMode = FMOD_SPEAKERMODE_DEFAULT;
        int rawSpeakers = 0;
        if (system_->getSoftwareFormat(
                &sampleRate_,
                &speakerMode,
                &rawSpeakers) != FMOD_OK)
        {
            sampleRate_ = 0;
        }

        if (system_->getMasterChannelGroup(&masterChannelGroup_) != FMOD_OK)
        {
            masterChannelGroup_ = nullptr;
        }
    }
}
