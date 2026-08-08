# UI 캔버스와 2D/3D 표면 매핑

## 설계 경계

UI는 상태/입력과 표시 방식을 분리한다.

```text
UiCanvas
└─ UiElement 트리
   ├─ UiPanel
   ├─ UiLabel
   ├─ UiButton
   ├─ UiToggle
   ├─ UiSlider
   ├─ UiCycleSelector
   ├─ UiComboBox
   └─ UiImage

화면 포인터 ────────────────┐
카메라 Ray → IUiSurface → UV ├→ Canvas 좌표 → UiInputRouter
                            ┘
```

- `UiCanvas`는 논리 크기, 요소 소유권, hit-test와 action queue를 가진다.
- `UiElement`의 `Bounds`는 부모의 좌측 상단을 원점으로 하며 아래쪽이 +Y다.
- `UiInputRouter`는 hover, press, pointer capture, release, click을 처리한다.
  펼쳐진 `UiComboBox` 위에서는 wheel event도 같은 입력 경로로 전달한다.
- `WorldSpaceCanvas`는 `UiCanvas`를 상속하지 않고 소유하며 `IUiSurface`를 합성한다.
  UI 문서와 표현 표면은 서로 다른 책임이기 때문이다.
- UI 코어는 Win32와 D3D12 타입을 사용하지 않는다. 다른 그래픽 백엔드에서는
  draw command를 소비하는 표현 어댑터만 교체한다.

## 입력 좌표 변환

화면 UI는 `MapScreenPointer`로 클라이언트 픽셀을 Canvas 좌표로 바꾼다.
월드 UI는 다음 순서를 사용한다.

1. `CreateWorldPointerRay`가 화면 픽셀과 view-projection 행렬로 월드 Ray를 만든다.
2. `PlaneUiSurface` 또는 `MeshUvUiSurface::Raycast`가 가장 가까운 표면을 찾는다.
3. 삼각형 표면은 Möller–Trumbore 충돌 결과의 barycentric weight로 정점 UV를
   보간한다.
4. `WorldSpaceCanvas::MapPointer`가 UV를 Canvas 논리 좌표로 바꾼다.
5. 이후 hit-test와 Widget 이벤트 처리는 화면 UI와 완전히 같다.

따라서 회전·이동·비균일 스케일된 평면과 UV가 있는 곡면 모두 같은 입력 경로를
사용한다. `MeshUvUiSurface`의 현재 기준 구현은 모든 삼각형을 순회하므로 작은 UI
메시에 적합하다. 큰 메시에서는 구현 내부에 BVH를 추가하되 공개 인터페이스는
변경하지 않는 것이 권장된다.

Win32 `InputState::MousePositionX/Y`는 UI 배치를 위한 현재 클라이언트 좌표
스냅샷이다. 리듬 판정에는 이 값이 아니라 기존 `InputState::Events()`의 Raw Input
QPC timestamp를 계속 사용해야 한다.

## 트리 기반 Z-Order와 hit-test

각 `UiElement` 부모는 하나의 stacking context다. 부모 배경을 먼저 그린 뒤 형제
자식을 `ZIndex`가 낮은 순서로 그린다. 같은 값에서는 삽입 순서를 유지하며, 한
자식의 전체 하위 트리는 다른 형제의 하위 트리와 섞이지 않는다. hit-test는 이
paint order의 정확한 역순을 사용하므로 화면에서 앞에 보이는 요소가 입력도 먼저
받는다. 펼친 `UiComboBox`를 형제보다 앞에 두려면 콤보박스 자체에 더 큰
`SetZIndex`를 지정한다.

## D3D12 표시

`D3D12UiRenderer`는 `UiCanvas::BuildDrawList()` 결과를 기존 mesh/text renderer로
전달한다. command의 순서는 위젯 트리의 paint order이며 화면 UI의 text glyph도
같은 순서에서 계산한 depth 값을 사용한다. 따라서 mesh 배치와 text batch의 실제
기록 시점이 달라도 부모·형제·자식 순서가 유지된다. 서로 다른 Canvas는
`SubmitScreen`의 `canvasZOrder`로 순서를 지정하며, 각 Canvas가 독립된 depth 구간을
사용하므로 한 Canvas의 command 수가 변해도 다른 Canvas와 순서가 뒤집히지 않는다.

- `SubmitScreen`: 사각형, PNG image command, DirectWrite 글자를 화면 픽셀 공간에
  표시한다.
- `SubmitPlane`: 사각형과 PNG image command를 임의의 월드 XY 평면에 표시한다.
- `CreateCanvasRenderTarget`: Canvas가 그려질 shader-resource/render-target 겸용
  RGBA8 텍스처를 만든다.
- `RenderToTexture`: 사각형과 DirectWrite 글자를 해당 텍스처에 그린 뒤 mesh가
  샘플링할 수 있는 상태로 전환한다.

`D3D12UiRenderer::LoadImage(path)`는 PNG를 texture set으로 올리고 `UiImageHandle`을
돌려준다. 이 핸들은 `UiImage::SetImage` 또는 `UiVisualStyle`의
`normalImage`/`hoveredImage`/`pressedImage`/`disabledImage`에 넣는다. 색상은 image의
알파를 보존하면서 tint로만 적용된다. 현재 곡면용 `RenderToTexture`는 사각형과 글자만
배치하므로 PNG widget은 screen 또는 plane 경로에서 사용해야 한다. 곡면 PNG까지
필요해질 때에는 texture-array 배치를 render-target pass에 추가한다.

글자를 포함한 전체 Canvas를 곡면에 표시할 때에는 `RenderToTexture`를 먼저 기록하고,
그 target의 `Textures()`를 사용하는 textured Material을 곡면 mesh에 연결한다.
`CurvedRectangleShape`는 이 경로를 바로 시험할 수 있는 수평 원호 primitive다. 보이는
메시와 입력용 `MeshUvUiSurface`에 반드시 같은 Shape와 world transform을 사용해야
표시된 Widget과 hit-test 위치가 일치한다.

현재 한 `D3D12UiRenderer`는 프레임당 Canvas texture pass 하나를 기록한다. 여러 월드
Canvas가 필요하면 renderer 인스턴스를 분리하거나 후속 다중-pass 배처를 추가한다.
render target은 `D3D12UiRenderer::Shutdown`보다 먼저 해제하고, 매 프레임 main scene
mesh를 제출하기 전이나 후에 `RenderToTexture`를 호출한 뒤 곡면 mesh를 제출한다.

## 수명과 이벤트

`UiElement` 자식은 `unique_ptr`로 부모가 소유한다. 포인터 capture 중 요소를 삭제할
수 있으므로 Router는 raw pointer를 장기 보관하지 않고 `UiElementId`로 매번 다시
찾는다. 한 Update가 끝난 뒤 `UiCanvas::TakeActions()`로 `Clicked`, `ValueChanged`,
`SelectionChanged`를 소비한다.

`UiCycleSelector`는 API처럼 항목 수가 작은 설정에 쓰는 click-to-advance control이다.
`UiComboBox`는 선택 필드를 클릭하면 아래에 popup 목록을 표시한다. 목록이
`SetMaxVisibleItems`보다 길면 popup의 휠 또는 항목 영역 드래그로 행을 스크롤한다.
popup은 부모 bounds 밖에서도 hit-test할 수 있다. 다른 형제보다 앞에 표시하고
입력받아야 한다면 콤보박스에 더 큰 `ZIndex`를 지정한다. 키보드 탐색과 focus
순서는 후속 접근성/focus 계층에서 확장할 기능이다.

`UiLabel`도 `UiElement`이므로 `SetStyle`로 배경색을 지정할 수 있다. caption, 상태
문구, 도움말처럼 텍스트만 표시하는 영역도 같은 방법으로 독립된 배경과 hover/disabled
색을 가질 수 있다.

```cpp
auto& deviceIndex = canvas.Root().EmplaceChild<mrg::ui::UiComboBox>();
deviceIndex.SetBounds({20.0F, 20.0F, 240.0F, 36.0F});
deviceIndex.SetItems({L"Driver 0", L"Driver 1", L"Driver 2", L"Driver 3"});
deviceIndex.SetMaxVisibleItems(3);
deviceIndex.SetItemHeight(32.0F);
```

```cpp
mrg::ui::UiCanvas canvas({320.0F, 180.0F});
auto& button = canvas.Root().EmplaceChild<mrg::ui::UiButton>(L"Apply");
button.SetBounds({20.0F, 20.0F, 120.0F, 40.0F});

mrg::ui::UiInputRouter router;
router.Process(canvas, pointerInput);
for (const mrg::ui::UiAction& action : canvas.TakeActions())
{
    if (action.source == button.Id() &&
        action.type == mrg::ui::UiActionType::Clicked)
    {
        // Apply options.
    }
}
```
