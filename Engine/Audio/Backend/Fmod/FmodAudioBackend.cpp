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

        const char* OutputBackendName(
            const AudioOutputBackend backend) noexcept
        {
            switch (backend)
            {
            case AudioOutputBackend::Wasapi:
                return "WASAPI";
            case AudioOutputBackend::Asio:
                return "ASIO";
            case AudioOutputBackend::NoSound:
                return "NoSound";
            case AudioOutputBackend::Automatic:
            default:
                return "Automatic";
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
        std::string enumerationError;
        if (!RefreshOutputDevices(enumerationError))
        {
            // Device discovery failure does not prevent the automatic output
            // path from initializing, but the exact FMOD stage remains in the
            // Visual Studio Debug Output for diagnosis.
            DebugLog(enumerationError);
        }

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

    bool FmodAudioBackend::RefreshOutputDevices(std::string& errorMessage)
    {
        // Build a replacement list first so a temporary probe failure does
        // not erase a device snapshot that was previously usable.
        std::vector<AudioDeviceInfo> refreshedDevices;
        refreshedDevices.push_back(AudioDeviceInfo{
            AudioOutputBackend::Automatic,
            -1,
            "Windows default output (FMOD automatic)",
            0,
            0});

        std::string wasapiError;
        std::string asioError;
        const bool wasapiEnumerated = EnumerateDevices(
            AudioOutputBackend::Wasapi,
            refreshedDevices,
            wasapiError);
        const bool asioEnumerated = EnumerateDevices(
            AudioOutputBackend::Asio,
            refreshedDevices,
            asioError);

        if (!wasapiEnumerated || !asioEnumerated)
        {
            errorMessage.clear();
            if (!wasapiEnumerated)
            {
                errorMessage += wasapiError;
            }
            if (!asioEnumerated)
            {
                if (!errorMessage.empty())
                {
                    errorMessage += " | ";
                }
                errorMessage += asioError;
            }

            // A successful ASIO probe must still become visible when WASAPI
            // probing failed (and vice versa). For each failed output type,
            // retain only its last known entries instead of discarding them.
            const auto preservePreviousBackend =
                [this, &refreshedDevices](
                    const AudioOutputBackend failedBackend)
            {
                for (const AudioDeviceInfo& device : outputDevices_)
                {
                    if (device.backend == failedBackend)
                    {
                        refreshedDevices.push_back(device);
                    }
                }
            };
            if (!wasapiEnumerated)
            {
                preservePreviousBackend(AudioOutputBackend::Wasapi);
            }
            if (!asioEnumerated)
            {
                preservePreviousBackend(AudioOutputBackend::Asio);
            }
            outputDevices_ = std::move(refreshedDevices);
            return false;
        }

        outputDevices_ = std::move(refreshedDevices);
        errorMessage.clear();
        return true;
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

    bool FmodAudioBackend::EnumerateDevices(
        const AudioOutputBackend backend,
        std::vector<AudioDeviceInfo>& destination,
        std::string& errorMessage)
    {
        // Phase 1: create an uninitialized probe. FMOD driver enumeration is
        // specific to the selected output type and should happen before init.
        FMOD::System* probe = nullptr;
        FMOD_RESULT result = FMOD::System_Create(&probe);
        if (result != FMOD_OK || probe == nullptr)
        {
            errorMessage = MakeFmodError("FMOD::System_Create", result) +
                " while enumerating " + OutputBackendName(backend);
            return false;
        }

        const auto releaseProbe = [&probe, backend]()
        {
            const FMOD_RESULT releaseResult = probe->release();
            if (releaseResult != FMOD_OK)
            {
                DebugLog(
                    MakeFmodError("FMOD::System::release", releaseResult) +
                    " after enumerating " + OutputBackendName(backend));
            }
            probe = nullptr;
        };

        const FMOD_OUTPUTTYPE output = ToFmodOutput(backend);
        result = probe->setOutput(output);
        if (result != FMOD_OK)
        {
            errorMessage = MakeFmodError(
                "FMOD::System::setOutput",
                result) + " for " + OutputBackendName(backend);
            releaseProbe();
            return false;
        }

        // Phase 2: FMOD now reports only the drivers belonging to the output
        // selected above. A zero count is valid when no such driver exists.
        int driverCount = 0;
        result = probe->getNumDrivers(&driverCount);
        if (result != FMOD_OK)
        {
            errorMessage = MakeFmodError(
                "FMOD::System::getNumDrivers",
                result) + " for " + OutputBackendName(backend);
            releaseProbe();
            return false;
        }

        // Phase 3: keep every valid FMOD index. A malformed third-party ASIO
        // entry is skipped without preventing later valid drivers from being
        // exposed to the Client.
        int detectedCount = 0;
        for (int driverIndex = 0; driverIndex < driverCount; ++driverIndex)
        {
            std::array<char, 512> name{};
            FMOD_GUID guid{};
            int systemRate = 0;
            FMOD_SPEAKERMODE speakerMode = FMOD_SPEAKERMODE_DEFAULT;
            int speakerChannels = 0;

            result = probe->getDriverInfo(
                    driverIndex,
                    name.data(),
                    static_cast<int>(name.size()),
                    &guid,
                    &systemRate,
                    &speakerMode,
                    &speakerChannels);
            if (result != FMOD_OK)
            {
                DebugLog(
                    MakeFmodError("FMOD::System::getDriverInfo", result) +
                    " for " + OutputBackendName(backend) + " driver " +
                    std::to_string(driverIndex));
                continue;
            }

            destination.push_back(AudioDeviceInfo{
                backend,
                driverIndex,
                name.data(),
                systemRate,
                speakerChannels});
            ++detectedCount;
        }

        DebugLog(
            std::string(OutputBackendName(backend)) + " enumeration found " +
            std::to_string(detectedCount) + " of " +
            std::to_string(driverCount) + " FMOD drivers.");
        releaseProbe();
        errorMessage.clear();
        return true;
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
