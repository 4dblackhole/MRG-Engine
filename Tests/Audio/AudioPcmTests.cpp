#include "MRG_Core.h"
#include "AudioPcmTests.h"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
    void Require(bool value, const std::string& message)
    {
        if (!value)
            throw std::runtime_error(message);
    }
    struct PcmProbe
    {
        std::uint32_t channels{}, rate{};
        std::uint64_t frames{}, attackFrame{UINT64_MAX};
        std::vector<float> samples;
    };
    PcmProbe Probe(const std::filesystem::path& path)
    {
        PcmProbe pcm;
        mrg::audio::DecodeAudioPcm(path, {}, [&](std::uint32_t channels, std::uint32_t rate,
                                               std::span<const float> samples)
        {
            Require(channels > 0 && rate > 0 && samples.size() % channels == 0,
                    "Decoder provides complete PCM frames and a source format.");
            Require(samples.size() / channels <= 4096, "Decoder delivers bounded chunks.");
            pcm.channels = channels;
            pcm.rate = rate;
            for (std::size_t i = 0; i < samples.size(); ++i)
                if (pcm.attackFrame == UINT64_MAX && std::abs(samples[i]) > .0316227766F)
                    pcm.attackFrame = pcm.frames + i / channels;
            pcm.frames += samples.size() / channels;
            pcm.samples.insert(pcm.samples.end(), samples.begin(), samples.end());
        });
        return pcm;
    }
    void WaitReady(mrg::audio::AudioSystem& audio, mrg::audio::AudioClip& clip)
    {
        std::string error;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
        while (true)
        {
            audio.Update();
            const auto state = clip.LoadState(error);
            Require(state != mrg::audio::AudioClipLoadState::Failed, error);
            if (state != mrg::audio::AudioClipLoadState::Loading)
                return;
            Require(std::chrono::steady_clock::now() < deadline, "PCM clip loading timed out.");
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    }
    void WaitVoice(mrg::audio::AudioSystem& audio, mrg::audio::AudioVoice& voice)
    {
        std::string error;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
        while (true)
        {
            audio.Update();
            const auto state = voice.PreparationState(error);
            Require(state != mrg::audio::AudioPreparationState::Failed, error);
            if (state == mrg::audio::AudioPreparationState::Ready)
                return;
            Require(std::chrono::steady_clock::now() < deadline, "PCM source seeking timed out.");
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    }
    void VerifyPlayback(const std::filesystem::path& path, const PcmProbe& pcm)
    {
        using namespace mrg::audio;
        AudioSystem audio;
        AudioConfig config;
        config.preferredBackend = AudioOutputBackend::NoSound;
        std::string error;
        Require(audio.Initialize(config, CreateFmodAudioBackend, CreateFmodAudioClipBackend, error), error);
        for (auto mode : {AudioLoadMode::Sample, AudioLoadMode::SampleAsync, AudioLoadMode::StreamAsyncAccurate})
        {
            auto clip = audio.LoadSound(path, mode, error);
            Require(clip != nullptr, error);
            WaitReady(audio, *clip);
            std::uint64_t duration{};
            Require(clip->GetDurationMicroseconds(duration, error), error);
            const auto expected = static_cast<std::uint64_t>(std::llround(
                static_cast<long double>(pcm.frames) * 1'000'000 / pcm.rate));
            Require(duration == expected, "Playback and analysis expose exactly the same PCM frame count.");
            AudioPlaybackSettings settings;
            settings.startPaused = true;
            settings.startPositionMicroseconds = duration / 3;
            auto voice = clip->Play(settings, nullptr, error);
            Require(voice != nullptr, error);
            WaitVoice(audio, *voice);
            for (auto position : {std::uint64_t{0}, duration / 2, duration - 1, std::uint64_t{0}})
            {
                settings.startPositionMicroseconds = position;
                Require(voice->Restart(settings, nullptr, error), error);
                WaitVoice(audio, *voice);
            }
            Require(voice->Stop(error), error);
            settings.startPositionMicroseconds = duration;
            Require(clip->Play(settings, nullptr, error) == nullptr,
                    "The normalized PCM end position is rejected before allocating a voice.");
        }
        // Cancellation/shutdown must not leave a decoder referencing the device.
        auto cancelled = audio.LoadSound(path, AudioLoadMode::StreamAsyncAccurate, error);
        audio.RetireClip(std::move(cancelled));
        audio.Shutdown();
    }
}

int TestAudioPcm(const char* source)
{
    try
    {
        const auto fixture = std::filesystem::path(__FILE__).parent_path() / "Fixtures/PaddedTone.mp3";
        auto pcm = Probe(fixture);
        Require(pcm.rate == 44100 && pcm.channels == 2 && pcm.frames == 44100,
                "LAME priming and end padding are removed while preserving the exact source length.");
        const double attack = static_cast<double>(pcm.attackFrame) * 1000 / pcm.rate;
        Require(attack >= 249 && attack < 252,
                "Intentional 250 ms leading silence is preserved without an MP3 frame shift.");
        VerifyPlayback(fixture, pcm);
        std::stop_source stop;
        stop.request_stop();
        bool called{};
        mrg::audio::DecodeAudioPcm("must-not-open-a-cancelled-file", stop.get_token(),
                                  [&](auto, auto, auto) { called = true; });
        Require(!called, "Cancelled reads do not open the file or invoke consumers.");
        std::stop_source partial;
        std::uint64_t frames{};
        mrg::audio::DecodeAudioPcm(fixture, partial.get_token(), [&](auto channels, auto, auto samples)
        {
            frames += samples.size() / channels;
            if (frames)
                partial.request_stop();
        });
        Require(frames == 4096, "Cancellation stops after the first delivered PCM block.");
        if (source)
        {
            const std::filesystem::path path(std::u8string_view{reinterpret_cast<const char8_t*>(source)});
            pcm = Probe(path);
            VerifyPlayback(path, pcm);
            std::cout << "Source rate=" << pcm.rate << " channels=" << pcm.channels
                      << " frames=" << pcm.frames << " first_attack_ms="
                      << static_cast<double>(pcm.attackFrame) * 1000 / pcm.rate << '\n';
        }
        std::cout << "PCM decoding, exact duration, async loading, seek/restart and cancellation passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "PCM test failed: " << error.what() << '\n';
        return 1;
    }
}
