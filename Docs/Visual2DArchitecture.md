# Visual2D 스프라이트·위젯 통합 구조

## 책임과 프로젝트 경계

`Visual2D`는 일반 Sprite와 옵션 위젯을 같은 노드, Transform, 입력 및 렌더링
경로로 처리한다. 독립 `MRG.UI` 프로젝트와 `UiElement` 상속 계층은 존재하지 않는다.

```text
MRG.Engine/Core/Visual2D
  Visual2DNode + Components + Canvas + Input + Surface + Widget factories

MRG.Graphics.D3D12/Visual2D
  D3D12Visual2DRenderer
```

`D3D12Visual2DRenderer`는 Scene이 생성하는 객체가 아니다. 엔진의
`D3D12Renderer`가 실행 중 하나만 생성하고 Mesh/Text 렌더러 다음에
초기화한다. 구체 D3D12 클래스는 엔진 내부 헤더에만 있으며 Client에는
생명주기 함수가 없는 `Visual2DRenderSystem` 계약만 공개된다. Client는
`EngineServices::visual2DRendering`으로 이미지와
렌더 타깃을 만들고, Render 중에는
`RenderContext::visual2DRendering`으로 Canvas를 제출한다. Scene은
Canvas, 입력 라우터, 리소스 핸들만 소유한다.

`Visual2DNode`는 상속용 인터페이스가 아니라 구체적인 컴포넌트 컨테이너다.

```text
일반 Sprite = Visual2DNode + SpriteVisualComponent
클릭 Sprite = 위 구성 + Collider + PointerReceiverComponent
Button       = 위 구성 + TextVisualComponent + ButtonBehaviorComponent
ComboBox     = Sprite + PointerReceiver + ComboBoxBehaviorComponent
```

`CreateSprite`, `CreateButton`, `CreateComboBox` 등의 팩토리는 자주 쓰는 조합을
안전하게 구성할 뿐 별도 Widget 상속 객체를 생성하지 않는다. 컴포넌트는
`AddComponent<T>`, `GetComponent<T>`, `RemoveComponent<T>`로 실행 중 변경할 수 있다.

## Transform과 소유권

Canvas의 `Visual2DNode` 트리가 모든 노드의 유일한 소유자다. 각 노드는
`TransformNode`를 값으로 소유하고 Transform의 부모·자식 연결은 비소유 관계다.
따라서 노드와 Transform이 같은 자식을 각각 `unique_ptr`로 소유하지 않는다.

노드 위치는 부모 또는 화면 앵커에서 Pivot까지의 거리다. 실제 Transform의
좌측 상단 위치는 다음과 같이 계산된다.

```text
Transform top-left = node position - normalized pivot × node size
```

XYZ 회전, 크기, 위치와 부모 Transform이 Sprite, 글자, 충돌 영역에 동일하게
적용된다. 화면 포인터는 Canvas 광선으로 만든 뒤 각 노드의 월드 행렬 역변환을
거쳐 로컬 Collider에 전달하므로 회전된 사각형의 표시와 hit box가 일치한다.

## 720 세로 기준 좌표와 9개 앵커

기본 Canvas 기준은 1280×720이고 `CanvasScaleMode::FixedHeight`를 사용한다.

```text
pixelScale   = viewportHeight / 720
logicalHeight = 720
logicalWidth  = viewportWidth / pixelScale
```

100 논리 단위는 720p에서 100px, 1080p에서 150px다. 가로·세로에 같은 배율을
적용하므로 화면비가 바뀌어도 이미지와 글자가 찌그러지지 않는다.

Canvas는 렌더링과 hit-test를 하지 않는 다음 9개 앵커 노드를 항상 소유한다.

```text
TopLeft       TopCenter       TopRight
MiddleLeft    Center          MiddleRight
BottomLeft    BottomCenter    BottomRight
```

`CreateNode(Anchor::BottomRight)`로 만든 노드는 기본 Pivot도 `(1,1)`이 된다.
`SetBounds({-20,-20,w,h})`로 두면 화면비가 달라져도 우측·하단에서 20 논리 단위
안쪽에 고정된다. 기존 좌측 상단 좌표는 `Anchor::TopLeft`가 기본이다.

RenderTexture나 월드 표면처럼 크기가 고정되어야 하는 Canvas는
`CanvasScaleMode::Fixed`를 사용한다.

## 입력과 Z-Order

`Visual2DInputRouter`는 Canvas hit-test 결과를 enter, leave, press, move, release,
click, wheel 이벤트로 바꾼다. 누른 노드는 release까지 ID로 capture되므로 Slider와
ComboBox 드래그가 영역 밖에서도 이어진다. 삭제 전에 `Reset`하면 hover/capture가
안전하게 해제된다.

각 부모는 하나의 stacking context다. 형제는 `ZIndex` 오름차순으로 그리고,
같은 값은 삽입 순서를 유지한다. Hit-test는 그 정확한 역순이다. 렌더러는 투명
순서를 깨지 않도록 트리에서 평탄화된 순서를 유지한다.

## Sprite 이미지와 인스턴싱

`Visual2DRenderSystem::LoadImage`는 서로 크기가 다른 PNG를 각각 독립된
`Texture2D`로 올린다. 같은 Descriptor Table 안의 texture index를 인스턴스 데이터로
전달하므로 크기를 강제로 통일하는 `Texture2DArray`가 필요하지 않다. 화면과 평면
Sprite는 같은 mesh/material 조합을 사용하여 `DrawIndexedInstanced` 배치에 들어간다.

`SpriteVisualComponent::SetUvTransform`으로 cover/crop UV를 인스턴스별로 지정할
수 있다. 화면 Visual2D는 Depth Write 대신 트리 순서를 기준으로 하며, 월드의
불투명 Mesh는 기존 Mesh 렌더러의 Depth 상태를 사용한다.

## 화면과 곡면

화면 입력은 다음 경로를 사용한다.

```text
Mouse pixel → MapScreenPointer → Canvas logical point → InputRouter
```

월드 또는 곡면 입력은 다음 경로다.

```text
Mouse pixel → CreateWorldPointerRay
→ PlaneVisual2DSurface 또는 MeshUvVisual2DSurface
→ UV → Canvas logical point → InputRouter
```

`WorldSpaceVisual2DCanvas`는 Canvas를 상속하지 않고 `Visual2DCanvas`와
`IVisual2DSurface`를 합성한다. 표시 Mesh와 입력 Surface에는 같은 Shape, UV 및
world transform을 사용해야 한다.

곡면은 `RenderToTexture`로 Canvas의 사각형, 서로 다른 크기의 Sprite 이미지와
글자를 먼저 그린 뒤 그 Texture를 Mesh에 샘플링한다. 이미지와 Text glyph도
노드 Transform을 받으므로 부모의 XYZ 회전과 크기 변경을 함께 따른다.

## 간단한 사용 예

```cpp
mrg::visual2d::Visual2DCanvas canvas;
canvas.SetViewportSize({1920.0F, 1080.0F});

auto& sprite = canvas.CreateNode(
    mrg::visual2d::Anchor::BottomCenter,
    "Character");
sprite.SetBounds({0.0F, -48.0F, 128.0F, 128.0F});
sprite.AddComponent<mrg::visual2d::SpriteVisualComponent>().SetImage(image);
sprite.AddComponent<mrg::visual2d::RectangleCollider2DComponent>();
sprite.AddComponent<mrg::visual2d::PointerReceiverComponent>(handler);

mrg::visual2d::Visual2DInputRouter input;
input.Process(canvas, pointerSnapshot);

renderContext.visual2DRendering->SubmitScreen(canvas, renderContext);
```
