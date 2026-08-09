# MRG-Engine 프로젝트 구조

이 저장소는 엔진 소스, 엔진 테스트와 문서만 보관한다. 게임 전용 Client는 별도
저장소에서 관리한다.

전체 기능과 신규 작업 시작 순서는 [EngineOverview.md](EngineOverview.md)를 먼저
참고한다. 이 문서는 실제 디렉터리와 프로젝트 경계만 집중해서 설명한다.

```text
MRG-Engine/
├─ Engine/
│  ├─ Platform.Win32/      Win32 창, Raw Input, 런타임 경로
│  ├─ Audio/               backend-neutral API와 FMOD backend
│  ├─ Collision/           렌더러 비종속 충돌 질의
│  ├─ Geometry/            정점 형식, Shape와 primitive
│  ├─ Core/Visual2D/       Sprite/Widget 컴포넌트, Canvas, 입력과 UV 표면
│  ├─ Graphics.D3D12/      D3D12 renderer, mesh, texture, text, shader, Visual2D 표현
│  ├─ Core/                loop, Client 계약, Scene, 공통 System 타입
│  └─ SDK/                 통합 MRG.Core.lib와 생성 MRG_Core.h
├─ Tests/
├─ Docs/
└─ MRG-Engine.sln
```

기능별 프로젝트는 원본 파일의 소유 경계를 나타낸다. 소비자는 이 모듈들을 여러
개 링크하지 않고 `Engine/SDK/MRG.Core.vcxproj` 하나만 참조한다.

## Client 연결 경계

Client가 지원받는 엔진 표면은 다음뿐이다.

```text
MRG_Core.h
MRG.Core.lib
```

소스 통합 시에는 `MRG.Core.vcxproj`를 프로젝트 참조로 연결한다. 바이너리 배포
시에는 동일 구성의 헤더와 라이브러리를 전달한다. 자세한 내용은
`EngineIntegration.md`를 참고한다.

게임별 Scene ID, asset, 입력에 따른 화면 전환 규칙과 게임 결과 데이터는 Client
저장소에 둔다.
