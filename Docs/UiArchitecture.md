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
   └─ UiComboBox

화면 포인터 ────────────────┐
카메라 Ray → IUiSurface → UV ├→ Canvas 좌표 → UiInputRouter
                            ┘
```

- `UiCanvas`는 논리 크기, 요소 소유권, hit-test와 action queue를 가진다.
- `UiElement`의 `Bounds`는 부모의 좌측 상단을 원점으로 하며 아래쪽이 +Y다.
- `UiInputRouter`는 hover, press, pointer capture, release, click을 처리한다.
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

## D3D12 표시

`D3D12UiRenderer`는 `UiCanvas::BuildDrawList()` 결과를 기존 mesh/text renderer로
전달한다.

- `SubmitScreen`: 사각형과 DirectWrite 글자를 화면 픽셀 공간에 표시한다.
- `SubmitPlane`: 사각형 draw command를 임의의 월드 XY 평면에 표시한다.
- `CreateCanvasRenderTarget`: Canvas가 그려질 shader-resource/render-target 겸용
  RGBA8 텍스처를 만든다.
- `RenderToTexture`: 사각형과 DirectWrite 글자를 해당 텍스처에 그린 뒤 mesh가
  샘플링할 수 있는 상태로 전환한다.

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

현재 `UiComboBox`는 단순 프레임 구현으로 클릭할 때 다음 항목을 선택한다. 팝업 목록,
키보드 탐색, focus 순서는 후속 접근성/focus 계층에서 확장할 기능이다.

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
