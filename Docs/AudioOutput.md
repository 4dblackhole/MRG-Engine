# 오디오 출력 선택과 짧은 효과음

`AudioSystem`은 Client에 FMOD 타입을 노출하지 않는 backend-neutral 서비스다. 현재
기본 backend는 시작할 때 FMOD probe system으로 WASAPI와 ASIO 출력 장치를 각각
열거하며, 각 `AudioDeviceInfo`에는 backend 종류와 해당 backend 안의 driver index가
들어 있다.

## 실행 중 장치 전환

`SelectOutputDevice`는 현재 backend를 즉시 파괴하지 않는다.

1. 선택한 backend/driver 설정으로 새 backend를 초기화한다.
2. `AudioSystem`에 등록된 모든 sound를 새 backend에 다시 생성한다.
3. 두 단계가 모두 성공한 경우에만 이전 backend를 종료하고 새 backend로 교체한다.

따라서 ASIO 초기화나 sound 재생성에 실패하면 오류를 반환하고 기존 출력과 기존
sound handle을 그대로 유지한다. 명시적으로 선택한 장치가 실패했을 때 WASAPI나
no-sound로 조용히 fallback하지 않는다.

```cpp
std::string error;
const auto sound = audio.LoadSound(path, error);
audio.SelectOutputDevice(audio.OutputDevices()[index], error);
audio.PlaySound(sound, error);
audio.UnloadSound(sound);
```

`AudioSoundHandle`은 backend 교체와 무관한 논리 handle이다. 실제 FMOD sound는
교체 과정에서 다시 만들어진다. 파일을 삭제하거나 이동한 뒤 장치를 전환하면 재생성에
실패할 수 있으므로 게임 실행 동안 등록한 원본 asset 경로를 유지해야 한다.

ASIO 장치는 제조사 driver가 설치된 경우에만 목록에 나타난다. 한 ASIO driver는 다른
프로그램과 배타적으로 충돌할 수 있으며, sample rate나 장치 상태 때문에 초기화가
실패할 수도 있으므로 UI는 실패 메시지를 표시하고 기존 선택을 유지해야 한다.
