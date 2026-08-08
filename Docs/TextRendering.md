# 텍스트 렌더링

`mrg::graphics::TextRenderSystem`은 글꼴, 색상, 크기, 정렬을 바꿀 수 있는
2D 텍스트를 D3D12 렌더 타깃에 그린다. DirectWrite는 글꼴 선택, 문자열
레이아웃, shaping과 글리프 분석을 담당하며, 실제 화면 합성은 엔진의 D3D12
글리프 아틀라스 파이프라인이 담당한다. Direct2D 및 D3D11-on-12는 사용하지
않는다.

## 수명과 호출 시점

1. `D3D12Renderer::Initialize`가 `TextRenderSystem`을 초기화한다.
2. Client의 `Initialize` 또는 장면의 `Initialize`에서 글꼴을 로드하고
   `FontHandle`을 보관한다.
3. 렌더 프레임마다 `D3D12Renderer::BeginFrame`이 텍스트 제출 목록을
   초기화한다.
4. Client/Scene의 `Render`에서 `RenderContext::textRendering`에 문자열을
   제출한다.
5. 루트 Client는 필요하다면 `SceneGameClient::OnClientRendered`에서 활성 Scene
   뒤에 전역 디버그 텍스트를 제출한다.
6. `D3D12Renderer::EndFrame`은 메시 draw를 먼저 기록하고 텍스트 draw를
   기록한다. 그러므로 일반 텍스트는 3D 장면 위에 합성된다.

`Submit`은 command list를 즉시 실행하지 않는다. DirectWrite 레이아웃 결과를
CPU 제출 목록에 저장하고, `Flush`가 아틀라스 page별로 정렬해
`DrawInstanced`를 기록한다.

## 글꼴 로드

```cpp
mrg::graphics::FontHandle fileFont =
    services.textRendering.LoadFontFile(
        L"assets/fonts/MyFont-Regular.ttf");

mrg::graphics::FontHandle systemFont =
    services.textRendering.LoadSystemFont(L"Segoe UI");
```

파일 글꼴은 DirectWrite custom font set으로 읽으므로 Windows에 설치하지
않아도 된다. 상대 경로의 기준은 실행 파일의 디렉터리다. 장면을 종료할 때
`FontHandle`을 해제하면 되며 DirectWrite COM 타입은 공개 API에 노출되지
않는다.

## 문자열 제출

```cpp
mrg::graphics::TextDrawCommand command;
command.positionPixels = {24.0F, 24.0F};
command.layoutSizePixels = {640.0F, 96.0F};
command.horizontalAlignment =
    mrg::graphics::TextHorizontalAlignment::Center;
command.verticalAlignment =
    mrg::graphics::TextVerticalAlignment::Center;
command.style.font = fileFont;
command.style.fontSizePixels = 32.0F;
command.style.color = {0.9F, 0.2F, 0.4F, 1.0F};

renderContext.textRendering->Submit(L"Hello, rhythm!", command);
```

`positionPixels`는 클라이언트 영역 좌상단 기준 위치다.
`layoutSizePixels`는 DirectWrite가 정렬과 줄바꿈에 사용할 사각형 크기다.
색상은 선형 RGBA 값이며 알파 블렌딩을 지원한다. `FontHandle`이 비어 있거나
크기/레이아웃이 유효하지 않으면 제출은 오류로 처리된다.

## 캐시와 GPU 처리

- 텍스트 레이아웃은 문자열, 글꼴, 크기, 레이아웃 크기와 정렬 조합으로
  캐시한다.
- rasterized 글리프는 글꼴 face, glyph index, pixel size 조합으로
  1024×1024 R8 아틀라스 page에 저장한다.
- 새 글리프만 upload heap에서 아틀라스로 복사한다.
- 글리프 instance는 프레임별 mapped upload buffer에 기록하고 atlas page별로
  묶어 draw한다.
- 업로드 리소스는 해당 frame resource를 다시 사용해도 안전해질 때까지
  보관한다. 글리프 추가 때문에 매 프레임 전체 GPU fence를 기다리지 않는다.

현재 구현은 일반 단색 글리프를 대상으로 한다. 컬러 이모지, underline,
strikethrough, inline object, SDF 확대 렌더링은 아직 지원하지 않는다.

## FPS/UPS 성능 오버레이

엔진은 `UpdateContext::performance`로 QPC 기반 FPS/UPS 표본만 전달한다.
글꼴 선택, F1 입력, 표시 여부와 화면 배치는 게임마다 달라지는 Client 책임이다.
따라서 엔진은 성능 오버레이 텍스트를 자동으로 제출하지 않는다.

`SceneGameClient`를 쓰는 Client는 `OnClientInitialized`, `OnClientUpdated`,
`OnClientRendered` 훅으로 글꼴을 생성하고, 최신 성능 표본을 문자열로 바꾸고,
활성 Scene 뒤에 텍스트를 제출할 수 있다. 자세한 계약은
[`PerformanceStatistics.md`](PerformanceStatistics.md)를 참고한다.
