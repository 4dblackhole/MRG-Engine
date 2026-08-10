# MRG-Engine

MRG-Engine은 Visual Studio 2022, C++20, DirectX 12 기반의 Windows 게임 엔진
저장소다. 게임 전용 코드는 포함하지 않으며, 소비 프로젝트에는 통합 정적
라이브러리 `MRG.Core.lib`와 단일 공개 헤더 `MRG_Core.h`를 제공한다.
3D `MeshInstance`는 Camera의 Perspective/Orthographic 절두체와 로컬 Bounding
Sphere를 사용해 화면 밖 인스턴스를 제출 전에 제외한다.

처음 사용하는 개발자나 새 Codex 세션은
[Docs/EngineOverview.md](Docs/EngineOverview.md)부터 읽는다. 주요 기능, 코드 위치,
객체 수명, Client 경계, 기능별 문서와 작업 체크리스트를 한곳에서 확인할 수 있다.

## 저장소 구성

```text
Engine/       플랫폼, 오디오, 충돌, 도형, D3D12 그래픽, 코어와 SDK
Tests/        공개 SDK 헤더만 사용하는 엔진 테스트
Docs/         엔진 통합 및 기능 문서
MRG-Engine.sln
```

기능별 모듈 프로젝트는 독립 개발과 탐색에 사용한다. 실제 게임은
`Engine/SDK/MRG.Core.vcxproj` 하나만 프로젝트 참조로 연결한다.

## 요구 사항

- Visual Studio 2022, MSVC v143, Windows 10/11 SDK
- FMOD Studio API for Windows
- `FMOD_ROOT` 환경 변수 또는 `Directory.Build.props`가 탐지하는 기본 FMOD 경로

FMOD SDK 헤더, import library와 DLL은 라이선스상 이 저장소에 포함하지 않는다.

## 빌드

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' `
  MRG-Engine.sln /m /t:Rebuild /p:Configuration=Debug /p:Platform=x64

& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' `
  MRG-Engine.sln /m /t:Rebuild /p:Configuration=Release /p:Platform=x64
```

산출물은 `bin/x64/<Configuration>/`에 생성된다. 엔진 전체 안내는
[Docs/EngineOverview.md](Docs/EngineOverview.md), 자세한 연결 방법은
[Docs/EngineIntegration.md](Docs/EngineIntegration.md)를 참고한다.

## Client 저장소에서 사용

현재 `MyRhythmGame-Client`는 이 저장소를
`Dependencies/MRG-Engine` Git submodule로 고정한다. Client 프로젝트는
submodule 내부의 `Engine/SDK/MRG.Core.vcxproj`만 참조하며 엔진의 기능 헤더를
직접 포함하지 않는다.
