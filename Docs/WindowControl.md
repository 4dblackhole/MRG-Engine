# 실행 중 창 크기 제어

`EngineServices::windowControl`은 Run이 소유하는 `platform::IWindowControl`의
비소유 포인터입니다. Initialize부터 Shutdown 시작까지 게임 루프 스레드에서만
사용합니다. 창이 없는 offscreen/test context에서는 nullptr일 수 있습니다.

- `ClientSize()`는 현재 게임 화면의 픽셀 크기를 반환합니다. 테두리/제목 표시줄은 제외합니다.
- `DisplayResolutions()`는 창이 위치한 모니터의 실제 지원 크기를 나열하고
  같은 크기의 주사율 변형을 중복 제거합니다. 장치 조회 오류는 예외로 보고합니다.
- `SetClientSize(size, error)`는 창의 client 영역을 즉시 변경합니다. 양수이며
  D3D12의 texture dimension 한도인 16,384px 이내의 크기만 허용합니다.
  최대화된 창은 복원하고 현재 위치·Z 순서를 유지합니다. 숨김 실행을 표시하지 않습니다.
  실패는 false와 오류 원문을 반환합니다.

Win32Window가 HWND와 메시지 처리를 소유합니다. WM_SIZE가 만든 변경 요청을
다음 Run 루프에서 renderer.Resize → Client.OnResize 순으로 전달하므로
옵션 콜백 안에서 Scene을 재진입하거나 GPU 자원을 교체하지 않습니다.
Client의 게임별 옵션·파일 저장·복구 정책은 엔진에 포함하지 않습니다.

`PhantomCore.Collision.Tests.exe --window-control`은 실제 숨김 D3D12/NoSound 실행으로
960×540 변경, native 지원 크기의 중복 제거, 잘못된 크기 거부,
지연된 OnResize 및 렌더러 크기·종료 수명을 검사합니다.
