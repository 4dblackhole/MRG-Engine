# Visual2D Renderer 소유권과 수명

## 소유 관계

Visual2D 렌더러는 `D3D12Renderer`가 하나만 소유한다. Scene이 렌더러를
생성하거나 `Initialize`, `Shutdown`을 호출하지 않는다. Client에는
`Visual2DRenderSystem`만 공개되고, 생명주기를 가진
`D3D12Visual2DRenderer`는 엔진 내부 헤더에 있으므로 소유권 제어를 위해
`friend class D3D12Renderer`를 사용하지 않는다.

```text
Run
└─ D3D12Renderer
   ├─ MeshRenderSystem
   ├─ TextRenderSystem
   └─ D3D12Visual2DRenderer (내부 구현)

Scene → Visual2DRenderSystem (비소유 계약)
├─ Visual2DCanvas
├─ Visual2DInputRouter
├─ ImageHandle
└─ RenderTargetTextureHandle
```

초기화 순서는 D3D12 장치, Mesh, Text, Visual2D 순서이고 종료는 그 반대다.
따라서 Visual2D가 공유 Mesh, Material, Texture, Font 핸들을 정리하는 동안
하위 렌더 시스템과 D3D12 장치는 항상 유효하다.

## Client 사용법

Scene 초기화에서는 엔진 서비스를 통해 이미지와 Canvas 렌더 타깃을 만든다.

```cpp
void ExampleScene::Initialize(const mrg::EngineServices& services)
{
    panelImage_ = services.visual2DRendering.LoadImage(imagePath);
    panelTarget_ =
        services.visual2DRendering.CreateCanvasRenderTarget(960, 630);
}
```

Render에서는 현재 프레임이 보유한 렌더러 포인터를 사용한다.

```cpp
void ExampleScene::Render(const mrg::graphics::RenderContext& context)
{
    context.visual2DRendering->SubmitScreen(canvas_, context);
    context.visual2DRendering->RenderToTexture(
        curvedCanvas_,
        panelTarget_,
        context);
}
```

`RenderContext::visual2DRendering`은 엔진이 연 프레임에서만 사용하는
비소유 포인터다. 보관하거나 삭제하면 안 된다.

## 여러 Canvas와 오프스크린 패스

하나의 렌더러로 한 프레임에 여러 Canvas를 화면이나 서로 다른 렌더
타깃에 그릴 수 있다. 프레임별 업로드 아레나는 각 Draw가 사용하는
인스턴스 범위를 선형 할당하므로 나중 패스가 앞선 패스의 데이터를
덮어쓰지 않는다. Text 렌더러도 같은 `renderIndex`의 여러 패스에 별도
범위를 할당한다.

RenderToTexture는 Canvas 트리의 그리기 순서를 유지하면서 연속된
Rectangle, 같은 이미지 Descriptor 페이지, Text를 각각 배치한다.
렌더 타깃 핸들은 해당 frame-resource fence가 완료될 때까지 렌더러가
보관하므로 동적 Scene이 프레임 직후 삭제되어도 GPU 참조가 유효하다.

## 이미지 캐시

이미지는 정규화된 경로를 키로 엔진 수명 동안 공유한다. 한 Descriptor
Table에는 서로 크기가 다른 Texture2D를 최대 64개 넣고, 64개를 넘으면
새 페이지를 만든다. 같은 페이지에 이미지를 추가할 때는 기존 64칸
Descriptor 블록의 빈 슬롯만 채우므로 이미지마다 새 블록을 만들지 않는다.

현재 정책은 실행 중 로드한 고유 이미지를 엔진 종료까지 캐시한다. 이 정책은
Scene 전환 중 GPU 리소스 파괴 문제를 피하고 재진입 시 업로드를 생략한다.
향후 매우 큰 사용자 스킨을 스트리밍해야 한다면 fence 기반 퇴거 정책을
별도의 AssetManager에 추가한다.

## 입력 소유권

렌더러를 하나로 통합해도 `Visual2DInputRouter`는 Canvas별로 유지한다.
Router에는 hover와 pointer capture 상태가 있으므로 서로 다른 Canvas가
하나의 Router를 공유해서는 안 된다. 여러 Canvas 사이의 우선순위는
Scene 수준 InputDispatcher 또는 LayerStack에서 결정한다.
