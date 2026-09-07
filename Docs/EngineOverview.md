# MRG-Engine 전체 안내서

이 문서는 MRG-Engine을 처음 사용하는 개발자와 새 Codex 세션을 위한 시작점이다.
엔진의 기능, 코드 위치, 객체 수명, Client 경계와 후속 상세 문서를 한곳에서 찾을
수 있도록 정리한다.

## 1. 가장 먼저 알아야 할 것

- 대상 환경은 Windows, Visual Studio 2022, C++20, DirectX 12다.
- 엔진은 하나의 정적 라이브러리 `MRG.Core.lib`와 하나의 공개 헤더
  `MRG_Core.h`로 Client에 제공된다.
- Client는 `Engine/SDK/MRG.Core.vcxproj` 하나만 참조하고 `MRG_Core.h`만
  포함한다.
- `Engine/SDK/MRG_Core.h`는 생성 파일이다. 직접 수정하지 않고 원본 기능 헤더를
  수정한 뒤 `Engine/SDK/GenerateCoreHeader.ps1`로 다시 생성한다.
- FMOD는 현재 기본 오디오 backend지만 공개 API에는 FMOD 타입을 노출하지 않는다.
- 게임 규칙, Scene ID, 게임별 asset과 화면 전환 정책은 Client 저장소에 둔다.

새 게임에서 엔진을 연결하는 방법은
[EngineIntegration.md](EngineIntegration.md), 현재 샘플의 실제 시작·종료 과정은
Client 저장소의 `Docs/ExecutionFlow.md`를 참고한다.

## 2. 5분 시작 경로

1. 저장소 루트의 `AGENTS.md`를 읽는다.
2. 이 문서에서 필요한 기능과 소유 프로젝트를 찾는다.
3. Client라면 `#include "MRG_Core.h"`만 사용한다.
4. `IGameClient`를 구현하거나 `SceneGameClient`를 상속한다.
5. `GameScene::Initialize`에서 리소스를 만들고, `Update`에서 상태를 바꾸며,
   `Render`에서는 렌더 시스템에 제출한다.
6. `Shutdown`에서 Scene 소유 handle을 해제한다.
7. Debug/Release x64 빌드와 관련 테스트를 모두 실행한다.

최소 진입점은 다음과 같다.

```cpp
#include "MRG_Core.h"

#include <memory>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    auto game = std::make_unique<MyGameClient>();
    return mrg::Run(std::move(game));
}
```

여러 Scene을 사용하는 게임은 `mrg::scene::SceneGameClient`를 상속하고
`RegisterScenes`에서 Scene factory와 수명 정책을 등록하는 방식이 권장된다.

## 3. 전체 구조와 주요 기능

| 영역 | 실제 위치 | 주요 책임과 공개 개념 |
| --- | --- | --- |
| 애플리케이션 | `Engine/Core/App` | `mrg::Run`, subsystem 생성·메인 루프·종료 순서 |
| Client 계약 | `Engine/Core/Client` | `IGameClient`, `SceneGameClient`, `EngineServices`, Update/Render context |
| Scene | `Engine/Core/Scene` | `GameScene`, `SceneManager`, factory, 지연 전환, 수명 정책 |
| 공통 시스템 | `Engine/Core/System` | `Camera`, `TransformNode`, `MeshInstance`, `HighResolutionClock` |
| 입력·창 | `Engine/Platform.Win32` | HWND, 메시지 펌프, Raw Input, Virtual-Key, QPC 입력 이벤트 |
| 오디오 | `Engine/Audio` | backend-neutral `AudioSystem`, 출력 API·장치·DSP buffer, FMOD backend |
| 충돌 | `Engine/Collision` | 렌더러 비종속 선·ray·면·원·구·OBB·삼각형·절두체 질의 |
| Geometry | `Engine/Geometry` | `Shape`, 정점 속성, 사각형·곡면·정육면체·구 primitive |
| D3D12 Graphics | `Engine/Graphics.D3D12` | device, swap chain, frame resource, mesh/material, texture, text, shader |
| Visual2D Core | `Engine/Core/Visual2D` | Sprite·위젯 공통 Node, component, Canvas, 입력, 평면·곡면 Surface |
| Visual2D 표현 | `Engine/Graphics.D3D12/Visual2D` | 엔진 소유 2D renderer, 화면·평면·render texture 제출 |
| 통합 SDK | `Engine/SDK` | `MRG.Core.lib`, 생성된 `MRG_Core.h` |

기능별 프로젝트는 코드를 찾고 독립적으로 개발하기 위한 경계다. 최종 Client가
여러 모듈 라이브러리를 직접 링크하는 구조가 아니라 `MRG.Core` 하나를 소비한다.

## 4. 실행과 객체 수명

```text
Client wWinMain
→ unique_ptr<IGameClient>
→ mrg::Run
   → HighResolutionClock / Win32Window / InputState 생성
   → D3D12Renderer와 하위 Mesh·Texture·Text·Visual2D 시스템 생성
   → AudioSystem과 선택한 backend 생성
   → EngineServices의 비소유 참조를 Client에 전달
   → Client Initialize
   → Update는 가능한 한 자주, Render는 주사율 deadline에만 실행
   → GPU 완료 확인
   → Client Shutdown
   → Audio / Graphics / Window 역순 해제
```

핵심 소유권 규칙은 다음과 같다.

- `mrg::Run`이 플랫폼, 그래픽, 오디오 subsystem을 소유한다.
- Client와 Scene이 받는 `EngineServices` 및 Render/Update context의 시스템 참조는
  빌린 참조다. 저장하거나 해제하지 않는다.
- `SceneManager`가 Scene factory와 생성된 Scene 객체를 소유한다.
- Scene은 `MeshInstance`, Canvas, 오디오 clip과 공유 GPU handle 같은 게임 객체를
  소유한다.
- Graphics의 frame resource는 제출된 mesh/material/texture handle을 fence 완료까지
  추가 보관하므로 동적 Scene 삭제 직후에도 GPU 참조가 안전하다.
- raw pointer는 명시적으로 다르게 문서화되지 않는 한 비소유 참조다.

상세 호출 순서와 종료 순서는 Client의 `Docs/ExecutionFlow.md`에 있다.

## 5. 시간·입력·렌더 스케줄

- `Update`는 `Sleep`이나 VSync 대기 없이 가능한 한 자주 호출된다.
- `Render`는 모니터 명목 주사율 또는 Client가 설정한 render deadline에 도달할
  때만 호출된다.
- 오디오 service update는 별도 deadline을 사용하며 렌더 속도를 제한하지 않는다.
- 키보드·마우스의 기준 입력은 Win32 Raw Input이다.
- 키 조회에는 자체 enum 대신 `VK_SPACE`, `VK_ESCAPE`, `'W'` 같은 Virtual-Key
  값을 사용한다.
- 리듬 판정은 렌더 프레임이나 현재 key state만 사용하지 않고
  `InputState::Events()`의 순서와 QPC timestamp를 사용해야 한다.
- 음악 동기화가 필요할 때는 FMOD DSP clock을 오디오 timeline의 기준으로 삼고,
  QPC와 DSP clock의 보정은 `AudioSystem` 경계 뒤에 둔다.

FPS/UPS 통계 데이터의 엔진·Client 경계는
[PerformanceStatistics.md](PerformanceStatistics.md)에 설명되어 있다.

## 6. Scene 사용 원칙

- 모든 경로는 `SceneManager::RegisterScene`으로 factory와
  `SceneRetention::KeepAlive` 또는 `DestroyOnExit` 정책을 등록한다.
- 모든 전환은 `SceneManager::ChangeScene`으로 요청한다.
- 전환은 활성 Scene의 `Update`가 반환된 뒤 적용되므로 Scene이 실행 중에
  자기 자신을 삭제하지 않는다.
- 게임별 Scene ID는 Client의 중앙 route catalog에 둔다.
- Scene 생성자에 하나의 “다음 Scene ID”를 넣지 않는다. 조건에 따라 여러 경로를
  선택하는 것은 Scene의 Update와 Client route 정책의 책임이다.
- 일회성 리소스는 `Initialize`/`Shutdown`, 활성화마다 바뀌는 상태는
  `BeginScene`/`EndScene`에 둔다.

현재 Client의 동적 Scene 예제는 `Docs/EngineFeatureExamples.md`와
`Client/GameScene/Examples`에서 확인할 수 있다.

## 7. Graphics·Geometry·인스턴싱

Geometry와 렌더링 객체는 역할이 분리되어 있다.

```text
Shape
  CPU 정점/인덱스와 primitive 형상

GpuMesh
  Shape에서 생성한 GPU vertex/index resource와 로컬 Bounding Sphere

MaterialInstance
  엔진 공통 root signature/PSO와 texture 설정

TransformNode
  부모·자식 위치, 회전, scale

MeshInstance
  GpuMesh + MaterialInstance + TransformNode의 Scene용 조합과 Camera 가시성 판정
```

`Shape`에는 D3D12 resource, Material, Render 함수나 Transform을 넣지 않는다.
Scene은 같은 `GpuMesh`와 `MaterialInstance`를 여러 `MeshInstance`가 공유하게 만들고,
`Render`에서 `MeshInstance::Submit`을 호출한다. `MeshRenderSystem`은 같은
mesh/material 조합을 모아 `DrawIndexedInstanced`로 기록한다.

`GpuMesh` 생성 시 Shape의 canonical position 정점으로 로컬 Bounding Sphere를 한
번 계산한다. `MeshInstance::Submit`은 부모를 포함한 월드 행렬로 Sphere 중심과
반지름을 변환하고 `Camera::Frustum()`에 완전히 벗어난 인스턴스는
`MeshRenderSystem`에 넘기지 않는다. Camera의 절두체는 View 또는 Projection이
바뀔 때만 다시 계산되며 D3D12 renderer는 Camera를 소유하거나 참조하지 않는다.

크기가 다른 이미지는 각각 독립 `Texture2D`로 원본 크기를 유지한다.
`TextureSet`은 이 리소스의 SRV descriptor 묶음이며 D3D11/12의 동일 크기
`Texture2DArray`가 아니다. 인스턴스마다 texture index와 UV transform을 바꿀 수
있다. `MakeCoverUvTransform`은 비율이 다른 표면을 빈 공간 없이 중앙 crop한다.

## 8. Visual2D·Sprite·위젯·곡면

Sprite와 버튼, 라벨, ComboBox는 별도 상속 계층이 아니라 같은
`Visual2DNode`에 component를 조합한다.

| 기능 | component 또는 객체 |
| --- | --- |
| 이미지·색상 | `SpriteVisualComponent` |
| 글자 | `TextVisualComponent` |
| 사각형·원·사용자 판정 | Collider component |
| pointer 이벤트 | `PointerReceiverComponent`, `Visual2DInputRouter` |
| 버튼·토글·슬라이더·ComboBox | behavior component와 widget factory |
| 부모·자식 변환 | 각 Node의 `TransformNode` |

Canvas는 전체 화면일 필요가 없다. HUD는 `FixedHeight`, 독립 패널은 필요한 크기의
`Fixed` Canvas로 만들 수 있다. 여러 Canvas를 렌더링할 때는 먼저
`canvasZOrder`, 그 다음 각 Canvas 내부 트리의 `ZIndex` 순서가 적용된다.
Canvas 논리 좌표는 정중앙 원점이며 3D Transform과 마찬가지로 `+Y`가 위쪽이다.
Win32 화면 픽셀은 입력·제출 경계에서 이 좌표로 변환된다.
단색·이미지 Sprite는 둥근 모서리를 지정할 수 있으며 node-local clip rect는
자식 draw packet과 hit-test에 함께 적용된다.

곡면 UI는 Canvas를 render texture에 그린 뒤 UV가 있는 mesh에 샘플링한다.
`MeshUvVisual2DSurface`는 로컬 공간 BVH로 pointer ray를 가속하고 barycentric UV를
Canvas 좌표로 변환한다.

설계와 사용법은 다음 문서에서 이어진다.

- [Visual2DArchitecture.md](Visual2DArchitecture.md)
- [Visual2DRendererLifecycle.md](Visual2DRendererLifecycle.md)
- [Visual2DPerformanceAndOwnership.md](Visual2DPerformanceAndOwnership.md)
- Client `Docs/Visual2DGuide.md`와 `Docs/Visual2DExample.md`

## 9. 오디오

`AudioSystem`은 Client가 사용하는 공개 facade이며 현재 구현은
`FmodAudioBackend`다.

- AUTO, WASAPI, ASIO 같은 output API 변경
- 현재 output의 driver snapshot 조회와 driver index 선택
- DSP buffer length와 buffer count 조회·변경
- Client 소유 `AudioClip`과 예약 재생 `AudioVoice` 생성
- `SceneGameClient::AudioPlayback()`의 공통 재생 관리: Voice ID 제어, 재생 중 Clip/Bus
  유지, Scene 전환 중 재생 유지와 Client 종료 시 정지
- 계층적 `AudioBus`, DSP effect, sample-accurate volume fade point
- DSP/QPC clock snapshot 및 출력 지연 정보 제공

output API를 변경할 때 backend가 다시 초기화되고 그 시점에만 장치 목록을 다시
조사한다. wav/음악 clip은 게임 asset이므로 Client가 소유한다. FMOD SDK와 DLL은
저장소에 포함하지 않으며 배포 게임이 FMOD 라이선스 요건을 충족해야 한다.

출력 초기화는 [AudioOutput.md](AudioOutput.md), 예약 재생과 mixer graph는
[AudioPlayback.md](AudioPlayback.md)를 참고한다.

## 10. 충돌

충돌 기능은 렌더링과 Scene 소유권에 의존하지 않는 stateless 질의다. 무한 직선,
ray, 유한 선분을 구분하며 경계 접촉도 충돌로 취급한다. 잘못된 음수 크기나 0 방향
line/ray는 예외 대신 충돌 없음으로 처리한다.

제공되는 주요 질의에는 plane-line/ray/segment, circle-line/OBB,
sphere-ray/OBB, OBB-OBB, triangle-ray와 ViewFrustum-sphere 3단계 분류가 포함된다.
전체 목록과 예제는
[Collision.md](Collision.md)에 있다.

## 11. Text

공개 Text API는 위치, 레이아웃 크기, 정렬, 글꼴 크기와 색상을 픽셀 단위로 받는다.
내부 구현은 DirectWrite 레이아웃과 D3D12 glyph atlas를 사용한다. Client 배포
글꼴은 라이선스 파일을 함께 보관해야 한다.

사용 예제와 자원 수명은 [TextRendering.md](TextRendering.md)를 참고한다.

## 12. 작업 위치 결정표

| 변경하려는 내용 | 저장소와 위치 |
| --- | --- |
| 게임 규칙, 판정, 결과, Scene ID | Client |
| 게임 전용 Scene, 위젯 배치·문구·asset | Client |
| 재사용 가능한 Scene/Transform/Input 계약 | `Engine/Core` 또는 `Platform.Win32` |
| backend-neutral 오디오 API | `Engine/Audio/System` |
| FMOD 호출과 output 구현 | `Engine/Audio/Backend/Fmod` |
| CPU 형상·정점·primitive | `Engine/Geometry` |
| stateless 충돌 수학 | `Engine/Collision` |
| D3D12 resource, PSO, shader, batching | `Engine/Graphics.D3D12` |
| Sprite·위젯 공통 Node와 입력 정책 | `Engine/Core/Visual2D` |
| D3D12 Visual2D 표현 | `Engine/Graphics.D3D12/Visual2D` |
| Client에 공개할 새 헤더 | 원본 기능 헤더 + `GenerateCoreHeader.ps1` 목록 |

판단 기준은 “다른 게임에서도 같은 의미로 재사용되는가”다. 게임 규칙과 표현
정책은 Client에, backend와 무관한 재사용 계약은 Core에, API별 구현은 해당
backend 프로젝트에 둔다.

## 13. 문서 색인

| 문서 | 읽어야 할 때 |
| --- | --- |
| [ProjectStructure.md](ProjectStructure.md) | 저장소·프로젝트 경계를 찾을 때 |
| [EngineIntegration.md](EngineIntegration.md) | 새 Client나 바이너리 SDK를 연결할 때 |
| [AudioOutput.md](AudioOutput.md) | FMOD, WASAPI/ASIO, 장치와 DSP buffer를 다룰 때 |
| [AudioPlayback.md](AudioPlayback.md) | DSP 예약 재생, bus, voice와 effect를 다룰 때 |
| [Collision.md](Collision.md) | 충돌 타입과 질의 계약을 사용할 때 |
| [TextRendering.md](TextRendering.md) | 글꼴·글자·glyph atlas 수명을 다룰 때 |
| [PerformanceStatistics.md](PerformanceStatistics.md) | FPS/UPS 수집과 표시 책임을 구분할 때 |
| [Visual2DArchitecture.md](Visual2DArchitecture.md) | Sprite·위젯·Canvas·입력 구조를 이해할 때 |
| [Visual2DRendererLifecycle.md](Visual2DRendererLifecycle.md) | 엔진 소유 Visual2D renderer와 Scene 경계를 볼 때 |
| [Visual2DPerformanceAndOwnership.md](Visual2DPerformanceAndOwnership.md) | BVH, 최소 Canvas, Z-Order와 GPU 소유권을 볼 때 |

Client 저장소의 추가 문서는 다음과 같다.

| Client 문서 | 내용 |
| --- | --- |
| `Docs/ExecutionFlow.md` | 실제 샘플의 시작, 프레임, 종료와 객체 수명 |
| `Docs/EngineFeatureExamples.md` | 키 1/2/3으로 진입하는 Mesh·충돌·위젯 예제 |
| `Docs/Visual2DGuide.md` | Sprite·위젯 생성, 입력, PNG, Canvas와 Z-Order |
| `Docs/Visual2DExample.md` | 화면·곡면 옵션 Canvas와 오디오 패널 구현 |
| `Docs/PerformanceOverlay.md` | Client 소유 FPS/UPS 표시 구현 |

## 14. 새 Codex 세션 체크리스트

1. 현재 작업 저장소의 `AGENTS.md`를 끝까지 읽는다.
2. 엔진 작업이면 이 문서, Client 작업이면 `Docs/ExecutionFlow.md`도 읽는다.
3. `git status -sb`, 현재 branch, `origin/main`과 submodule gitlink를 확인한다.
4. 요청을 엔진 재사용 기능과 Client 게임 정책으로 나눈다.
5. 공개 API 변경 전 원본 header와 `GenerateCoreHeader.ps1`을 함께 확인한다.
6. 기존 사용자 변경과 미추적 asset을 보존한다.
7. 긴 함수는 동작별 helper로 분리하고, 한 작업이 길 때는 단계 경계 주석을 둔다.
8. 엔진 변경은 Debug/Release x64와 관련 공개 SDK 테스트를 검증한다.
9. Client 변경은 Debug/Release x64와 두 구성의 `--smoke-test`를 검증한다.
10. 엔진과 Client를 함께 바꾸면 엔진을 먼저 병합하고 Client gitlink를 그
    `main` commit으로 갱신한다.

이 체크리스트는 구현 방법보다 저장소 경계와 검증 누락을 방지하기 위한 것이다.
세부 동작은 반드시 해당 기능 문서와 현재 코드를 함께 확인한다.
