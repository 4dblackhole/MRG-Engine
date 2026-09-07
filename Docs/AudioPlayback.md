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

## SceneGameClient 공통 재생 관리

`SceneGameClient::AudioPlayback()`은 Client 전체 수명의 `AudioPlaybackManager`를
제공한다. Scene factory에는 `std::ref(AudioPlayback())`로 같은 관리자를 전달할 수
있다. Scene 전환은 이 관리자의 재생을 자동으로 중단하지 않는다.

```cpp
// A SceneGameClient hook: registration/loading remains Client policy.
std::string error;
std::shared_ptr<mrg::audio::AudioClip> clip = services.audio.LoadSound(
    path, mrg::audio::AudioLoadMode::Stream, error);
const auto id = AudioPlayback().Play(clip, {}, nullptr, error);
if (auto* voice = AudioPlayback().FindVoice(id))
{
    static_cast<void>(voice->SetPaused(true, error));
}
static_cast<void>(AudioPlayback().Stop(id, error));
```

- `Play`는 Voice와 재생에 필요한 Clip/선택적 Bus를 보관하고 재사용되지 않는 ID를
  반환한다. 실패 시 ID는 0이며 오류 문자열을 제공한다.
- `FindVoice`의 포인터는 빌린 참조다. `Stop`, `StopAll`, 이후 `Update`가 자원을
  정리할 수 있으므로 오래 저장하지 말고 ID로 다시 조회한다.
- `SceneGameClient::Update`는 Scene과 Client hook 갱신 후 끝난 재생을 정리한다.
  일시정지 중이거나 DSP 시각에 예약된 Voice도 재생이 유효한 동안 유지한다.
- `Stop`은 native stop을 명시적으로 호출한다. 실패하면 해당 재생은 계속 관리한다.
  `StopAll`은 종료용 best-effort 정지 후 자원을 해제한다. Client 종료와 초기화 실패
  시 Scene 정리를 마친 뒤 `StopAll`을 호출하며 그 다음 엔진이 AudioSystem을 종료한다.
- 관리자는 Client 스레드에서 사용한다. Bus의 부모와 별도로 생성한 DSP Effect는
  호출자가 필요한 기간 동안 유지해야 한다.

파일 경로/SoundId 목록, 음악·효과음 bus 이름, 리듬 시간의 DSP 변환은 Client 정책이다.
관리자는 파일을 자동 검색하거나 모든 음악을 캐시하지 않는다. Scene이 재생 종료를
원하면 자신의 ID만 `Stop`한다. 다른 Scene까지 이어질 BGM은 전역 관리자가 유지한다.

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
