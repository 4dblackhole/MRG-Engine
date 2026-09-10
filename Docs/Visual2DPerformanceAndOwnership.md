# Visual2D 성능·소유권 지침

## 곡면 입력 가속

`MeshUvVisual2DSurface`는 생성할 때 Shape의 위치·UV·인덱스를 복사하고 로컬 공간
BVH(Bounding Volume Hierarchy)를 한 번 만든다. 각 노드는 삼각형 묶음의 AABB를
보관한다. 포인터 입력 시 world ray를 메시의 로컬 공간으로 옮긴 다음 다음 순서로
충돌을 찾는다.

1. BVH 노드 AABB와 ray가 만나지 않으면 그 하위 트리를 건너뛴다.
2. leaf에서만 실제 ray-triangle 충돌과 barycentric UV 보간을 수행한다.
3. 이미 찾은 가장 가까운 거리보다 먼 AABB는 추가로 제거한다.

노드는 preorder 배열과 `escapeIndex`로 저장하므로 `Raycast` 중 스택이나 임시
컨테이너를 할당하지 않는다. QuadTree는 평면의 2D 영역 분할, Octree는 넓은 3D
공간의 동적 객체 조회에 더 알맞다. 현재 대상은 임의 방향·곡률의 정적 삼각형
메시와 ray 질의이므로 메시 로컬 BVH가 더 직접적이다. Surface의 world transform이
바뀌어도 BVH를 다시 만들 필요가 없다.

## Canvas 크기와 화면 원점

`Visual2DCanvas`는 전체 화면일 필요가 없다.

- HUD 전체 배치는 기본 `FixedHeight` Canvas와 `SetViewportSize`를 사용한다.
- 독립 패널은 실제 콘텐츠에 필요한 크기의 `Fixed` Canvas를 사용한다.
- 패널의 화면 위치는 내부 루트 노드를 이동시키지 않고 `SubmitScreen`의
  `screenOrigin`으로 지정한다.
- 입력에도 동일한 `screenOrigin`을 `MapScreenPointer`에 전달한다.

Canvas 내부 좌표는 정중앙 원점과 Y-up을 사용한다. `screenOrigin`은 Canvas
좌표가 아니라 Win32 화면 배치 경계이므로 예외적으로 패널의 좌상단 픽셀을
뜻한다. `MapScreenPointer`가 좌상단 원점 픽셀을 중앙 원점 Canvas 좌표로 바꾼다.

`MapScreenPointer`는 물리 viewport 밖의 점만 거부한다. Canvas 영역 밖이지만
화면 안인 좌표는 반환하므로 Slider·ComboBox가 pointer capture를 가진 동안에도
드래그와 release 좌표가 끊기지 않는다. capture가 없을 때 Canvas 영역을 차단할지는
Scene의 Canvas dispatcher가 결정한다.

## 두 단계 Z-Order

화면 Visual2D의 순서는 다음 두 단계다.

1. `SubmitScreen(..., canvasZOrder)`가 Canvas 전체의 depth band를 결정한다.
2. 각 band 안에서 `Visual2DCanvas::BuildDrawList`가 부모·자식 트리와 `ZIndex`에
   따라 노드를 배치한다.

따라서 뒤쪽 Canvas의 높은 `ZIndex` 노드가 앞쪽 Canvas의 낮은 `ZIndex` 노드보다
앞으로 나올 수 없다. 입력 dispatcher도 앞쪽 Canvas부터 검사해야 화면 표시와
클릭 우선순위가 일치한다.

## GPU 자원 소유권

엔진 소유 `D3D12Renderer` 하나가 `D3D12Visual2DRenderer` 하나를 소유한다.
Scene은 renderer 수명에 관여하지 않고 Canvas, InputRouter, 이미지/렌더 타깃의
공유 핸들만 소유한다.

화면 Canvas는 `ScreenVisual2DManager::CreateOwnedCanvas`가 반환하는 이동 전용
`ScreenCanvasHandle`로 소유한다. handle을 Reset하거나 Scene이 소멸하면 등록도
자동 해제되므로 예외 경로와 `DestroyOnExit` 전환에서 Canvas가 남지 않는다.
Node 포인터는 여전히 Canvas가 소유하는 비소유 관찰자이며 handle보다 먼저 버린다.

`TextureSet`과 `RenderTargetTexture`의 공개 계약은 읽기 전용 메타데이터와 수명만
노출한다. 실제 `ID3D12Resource`, descriptor handle, RTV heap, resource state는
`TextureManager.cpp` 내부의 D3D12 구현 객체가 소유한다. `TextureManager`는
`friend` 접근 없이 자신이 만든 구현 타입을 검증한 뒤 upload와 state transition을
수행한다. 이 경계 덕분에 Client가 D3D12 자원 소유권을 오해하거나 직접 해제할 수
없다.

## Client 경계

Canvas 트리, 입력 라우팅, Surface/BVH, D3D12 배치, texture upload는 재사용 가능한
엔진 기능이다. 특정 패널의 크기·이미지·문구·오디오 선택 동작과 여러 Canvas 사이의
입력 우선순위는 게임 정책이므로 Client에 둔다. 새 게임을 만들 때 엔진 코드를
수정하지 않고 이 정책과 Scene만 교체한다.
