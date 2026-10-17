# 예약 재생, 버스와 실시간 오디오 효과

이 문서는 출력 API와 장치 열거를 다루는 [AudioOutput.md](AudioOutput.md)에 이어,
게임 플레이 중 사용하는 backend-neutral 재생 계층을 설명한다. FMOD 타입은
`Engine/Audio/Backend/Fmod` 밖으로 노출되지 않는다.

## 비동기 위치 준비

Play/Restart의 성공은 요청 수락이며 비동기 탐색 완료를 의미하지 않습니다.
`AudioVoice::PreparationState(error)`는 Loading/Ready/Failed를 반환하며,
스트림 생성 내부 탐색 → 요청 위치 탐색 → 완료를 겹치지 않게 진행합니다.
호출자는 paused Voice를 유지하고 매 Update에서 확인한 뒤, Ready일 때 새 clock snapshot으로
예약하고 unpause합니다. 탐색 중 새 요청은 이전 native 탐색 완료 후 최신 목표로 이동합니다.
SetPaused(false)도 준비 중에는 실제 unpause를 완료 시점까지 미룹니다.
AudioPlaybackManager::Update도 준비를 진행합니다. 직접 생성한 Voice는 준비 완료까지
보관하고 직접 polling해야 하며, 준비 중 wrapper를 버리는 fire-and-forget은 지원하지 않습니다.

일시정지는 살아 있는 음악 Voice를 유지하고 목표 위치로 Restart할 수 있습니다.
Manager::Restart는 종료/탈취된 채널만 같은 ID 아래 새 Voice로 교체하고, 살아 있는 채널의
실제 오류는 보존합니다. 파일 끝 미만의 us가 반올림으로 끝 프레임이 되는 경우 마지막
유효 프레임으로 제한하며 파일 끝 이상의 요청은 계속 거부합니다.
FindVoice의 빌린 포인터는 종료 채널을 교체한 Restart에서도 무효가 되므로 호출 후 다시 조회합니다.

2026-10-17: Debug/Release x64 전체 Rebuild와 SDK 회귀(종료 중 Restart 교체,
활성 채널 실패 보존 포함)를 통과했습니다. 별도 공개 SDK NoSound 진단에서 실제 MP3
4회 재사용, 같은 채널의 0/1/5초 탐색, 종료 샘플 교체, 끝 1us 전 허용/끝 거부와
지연 Clip 정리 후 종료를 두 구성에서 확인했습니다. 실제 출력 장치 청취는 미검증입니다.

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
제어한다. `Restart()`는 기존 native Voice/Channel의 재생 위치를 지정한 시작 위치(기본 0)로 되돌리고
새 설정과 Bus를 적용하므로, 같은 짧은 효과음을 겹치지 않고 다시 시작할 때 별도
Channel을 만들지 않는다. Voice 객체를 버려도 재생은 계속되며 명시적으로
`Stop()`한 경우에만 정지한다. 긴 음악은 `AudioLoadMode::Stream`, 짧은 히트사운드는
`AudioLoadMode::Sample`로 로드한다.

## SceneGameClient 공통 재생 관리

`SampleAsync`는 Sample의 CREATESAMPLE에 NONBLOCKING을 적용합니다. StreamAsync처럼
LoadState가 Ready인지 확인한 뒤 재생해야 하며 기존 동기 Sample/Stream 값은 유지합니다.
SceneGameClient는 native Voice 종료 확인을 최대 500Hz(2ms 주기)로 실행합니다.
AudioPlaybackManager를 직접 사용하는 호출자의 Update는 즉시 검사하며, 입력 timestamp나
DSP 예약 정밀도에는 영향이 없습니다.

2026-10-12 source seek/duration/retire 변경은 Debug/Release x64 전체
`MRG-Engine.sln` Rebuild와 두 구성의 `MRG.Collision.Tests`로 검증했다.
SDK probe는 원본 us 위치·길이·DSP 예약 전달과 Loading Clip 유지/Ready 후 해제를 검사한다.
Client의 별도 에디터 회귀는 실제 MP3의 정확한 비동기 열기/끝부분 seek/정상 종료/범위 밖
위치 거부를 FMOD NoSound로 검사한다. 물리 장치 청취나 수동 UI 검증은 아니다.

`AudioLoadMode::StreamAsync`는 FMOD 비동기 파일 열기를 사용한다. 생성 직후의 clip은
재생 준비가 끝나지 않았을 수 있으므로 `AudioClip::LoadState(error)`를 polling한다.
`Ready`에서 재생하고 `Failed`의 오류를 처리한다. `Loading`은 열기/버퍼 작업 중,
`Playing`은 mixer가 사용 중인 상태다. 기존 Sample/Stream의 동기 로드는 유지한다.
Clip을 Loading/Playing에서 해제하면 native I/O 또는 mixer를 기다릴 수 있으므로
UI caller는 준비가 끝날 때까지 소유권을 유지하고, 정지한 clip도 Ready/Failed를 확인한 뒤
해제한다. 최종 시스템 종료에서는 RetireClip의 Sound를 먼저 release하여 native I/O가
끝나게 한 뒤 FMOD를 close합니다. 종료 도중에만 이 release가 기다릴 수 있으며,
런타임 Update/Scene 전환의 retire는 계속 비차단 polling을 사용합니다. 이 동작은
[FMOD Sound::getOpenState/release](https://www.fmod.com/docs/2.03/api/core-api-sound.html)에 따른다.

`StreamAsyncAccurate`는 비동기 열기에 `FMOD_ACCURATETIME`을 더한다. VBR 파일의
전체 길이/PCM seek 테이블 준비도 비동기 작업에 포함되므로 Ready까지 기다린다.
`AudioClip::GetDurationMicroseconds`는 원본 PCM frame 수와 기본 주파수로 길이를 계산한다.
`AudioPlaybackSettings::startPositionMicroseconds`는 출력 DSP clock과 독립적인 파일 위치이며
가장 가까운 원본 PCM frame으로 변환한다. 파일 끝 이상은 오류다.
원본 sample rate와 정확한 VBR seek 요건은 [FMOD Sound API](https://www.fmod.com/docs/2.03/api/core-api-sound.html)를 따른다.
비동기 stream seek도 Loading 상태를 거칠 수 있다. paused Voice로 준비하고 LoadState가
Ready/Playing이 된 후 `AudioVoice::SetDelay(startDspClock, endDspClock, error)`와 SetPaused(false)로
실제 시작을 예약한다. 기본 시작 위치 0인 새 Voice는 불필요한 seek를 수행하지 않는다.

UI 소유자가 취소/정지한 Clip은 `AudioSystem::RetireClip(shared_ptr)`로 소유권을 넘길 수 있다.
AudioSystem::Update는 Ready/Failed가 된 Clip을 해제하며 Loading/Playing은 유지한다.
Voice를 먼저 정지해야 하며 API/Update는 같은 Client thread에서 호출한다.
시스템 종료에서는 backend 작업을 먼저 종료하고 남은 retire 목록을 해제한다.
이는 다른 Clip/Voice의 소유권이나 SceneGameClient 공통 재생을 바꾸지 않는다.


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
- `Restart`는 기존 ID와 native Voice/Channel을 유지하면서 처음부터 재생하고,
  성공한 경우 새 Bus를 관리자가 대신 보관한다. 이미 끝나 native handle이 만료된
  Voice는 새 `Play`로 시작해야 한다.
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
