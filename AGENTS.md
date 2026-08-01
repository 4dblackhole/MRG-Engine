# MRG-Engine working agreement

This file applies to the entire repository.

## Purpose and boundary

- This repository contains reusable Windows game-engine code only.
- Build with C++20, Visual Studio 2022, Win32, DirectX 12, and FMOD behind the
  backend-neutral audio API.
- `Engine/SDK/MRG.Core.vcxproj` is the supported consumer project. It builds
  one `MRG.Core.lib` and generates the single public `MRG_Core.h` facade.
- Do not add game-specific assets, Scene IDs, gameplay rules, or private
  Client implementation here.
- Do not commit FMOD SDK files, import libraries, or runtime DLLs.

## Source rules

- Keep feature-first physical folders and keep a feature's `.h` and `.cpp`
  together. Keep `.vcxproj.filters` paths synchronized with physical paths.
- Public APIs must not expose FMOD or D3D12 implementation-only types.
- Client-facing headers are selected by
  `Engine/SDK/GenerateCoreHeader.ps1`; never hand-edit generated
  `Engine/SDK/MRG_Core.h`.
- Prefer RAII and `Microsoft::WRL::ComPtr`. Raw pointers are non-owning unless
  an API explicitly documents ownership.
- Keep the unlimited Update loop, QPC input timestamps, render deadline,
  independent audio update deadline, and frame-resource fence model intact.
- Raw Input uses Win32 Virtual-Key values directly; do not add an engine key
  enum.
- `SceneManager` owns Scene factories and lifetime policy. Register through
  `RegisterScene`, transition through deferred `ChangeScene`, and never let a
  Scene delete itself.
- Common shaders belong to `Engine/Graphics.D3D12/Shader` and are embedded at
  build time.

## Verification

After engine changes, rebuild `MRG-Engine.sln` in Debug and Release x64.
Run `bin/x64/<Configuration>/MRG.Collision.Tests.exe` after collision or public
SDK changes. Tests must include only `MRG_Core.h`.
