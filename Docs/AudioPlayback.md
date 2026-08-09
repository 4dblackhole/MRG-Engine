# 예약 재생, 버스와 실시간 오디오 효과

이 문서는 출력 API와 장치 열거를 다루는 [AudioOutput.md](AudioOutput.md)에 이어,
게임 플레이 중 사용하는 backend-neutral 재생 계층을 설명한다. FMOD 타입은
`Engine/Audio/Backend/Fmod` 밖으로 노출되지 않는다.

## 하나의 DSP 기준 타임라인

`AudioSystem::CaptureClockSnapshot()`은 가능한 한 가까운 시점의 DSP clock,
software mixer sample rate, QPC tick과 QPC frequency를 반환한다. Client는 이 값으로
게임의 단일 리듬 타임라인을 DSP clock에 고정하고 음악, BMS 사운드와 히트사운드를
모두 같은 clock domain에 예약한다. 렌더 프레임이나 Update 횟수는 오디오 예약 시각의
기준으로 사용하지 않는다.

```cpp
const mrg::audio::AudioClockSnapshot clock =
    audio.CaptureClockSnapshot();

mrg::audio::AudioPlaybackSettings playback;
playback.startDspClock = scheduledDspClock;
playback.volume = 0.85F;

std::unique_ptr<mrg::audio::AudioVoice> voice =
    clip->Play(playback, hitSoundBus.get(), error);
```

`startDspClock == 0`은 즉시 재생이다. `AudioVoice`는 pause, stop, volume과 pitch를
제어한다. Voice 객체를 버려도 재생은 계속되며 명시적으로 `Stop()`한 경우에만
정지한다. 긴 음악은 `AudioLoadMode::Stream`, 짧은 히트사운드는
`AudioLoadMode::Sample`로 로드한다.

## Mixer bus

`AudioSystem::CreateBus`는 Client 소유 `AudioBus`를 만든다. Parent를 생략하면
Master 아래에 연결되고 parent를 지정하면 계층적 버스 그래프를 구성한다.

```text
Master
├─ Music
├─ HitSound
├─ TickSound
└─ UserInputFeedback
```

버스는 volume, pitch, mute와 DSP clock fade point를 제공한다. `AddFadePoint`의
`ramp`가 true이면 FMOD의 fade ramp를 사용하므로 YME 볼륨 자동화를 frame rate와
독립적으로 재생할 수 있다.

## DSP effect

`AudioBus::AddEffect`는 LowPass, HighPass, Compressor, Delay, Reverb를 지원한다.
효과 객체는 `AudioEffectParameter`의 공통 파라미터만 노출하고 FMOD enum이나 DSP
pointer는 공개하지 않는다. 적용할 수 없는 파라미터는 명시적인 오류로 거절된다.

Reverb send/return은 별도의 Reverb return 버스와 dry 버스의 volume/fade 자동화를
조합해서 구성한다. 게임별 YME parser와 automation timeline은 Client 영역이며,
엔진은 그 결과를 sample clock에 적용하는 범용 기능만 담당한다.

## 수명과 mixer 재시작

Clip, Voice, Bus, Effect는 모두 Client 소유 RAII 객체다. Mixer sample rate나 DSP
buffer를 재시작하기 전에는 이 객체를 모두 해제해야 한다. 이 규칙은 재초기화로 인해
FMOD native handle이 조용히 무효화되는 상황을 막는다.
