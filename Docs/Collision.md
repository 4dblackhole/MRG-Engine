# 충돌 연산

`mrg::collision`은 렌더링, 장면 객체 소유권, D3D12 리소스와 무관한 stateless
충돌 판정 기능이다. Client에서는 다른 엔진 기능과 마찬가지로
`MRG_Core.h` 하나를 포함해 사용한다.

```cpp
#include "MRG_Core.h"
```

## 지원 타입

### 2D

- `Line2D`: 양방향으로 무한한 직선
- `LineSegment2D`: `start`와 `end` 사이의 유한 선분
- `Circle2D`: 중심과 반지름
- `Obb2D`: 중심, 로컬 반크기, 반시계 방향 회전각

지원 판정:

- 원과 무한 직선
- 원과 선분
- 원과 OBB
- OBB와 OBB
- 점에서 선분 또는 OBB까지 가장 가까운 점

### 3D

- `Line3D`: 양방향으로 무한한 직선
- `Ray3D`: 원점에서 진행 방향으로만 뻗는 반직선
- `LineSegment3D`: 두 점 사이의 유한 선분
- `Plane3D`: `dot(normal, point) = distanceFromOrigin` 평면
- `Sphere3D`: 중심과 반지름
- `Obb3D`: 중심, 로컬 반크기, quaternion 회전

지원 판정:

- 평면과 직선, ray 또는 선분
- 구와 직선, ray 또는 선분
- 구와 OBB
- OBB와 OBB

`Plane3D`의 normal은 정규화하지 않아도 된다. `Obb3D::orientation`은
`(x, y, z, w)` quaternion이며 판정 전에 내부에서 정규화된다.

## 직선과 선분의 차이

충돌 범위를 의도에 맞게 선택해야 한다.

```cpp
using namespace mrg::collision;

Circle2D note{{100.0F, 80.0F}, 16.0F};

// 화면 전체를 가로지르는 수학적 직선과 판정한다.
const bool touchesGuideLine = Intersects(
    note,
    Line2D{{0.0F, 64.0F}, {1.0F, 0.0F}});

// 실제 두 끝점 사이만 판정한다.
const bool touchesLaneSegment = Intersects(
    note,
    LineSegment2D{{20.0F, 64.0F}, {180.0F, 64.0F}});
```

방향 벡터의 길이는 1일 필요가 없다. 방향이 0인 `Line2D`, `Line3D`,
`Ray3D`는 유효한 직선이 아니므로 충돌하지 않는 것으로 판정한다. 시작점과
끝점이 같은 선분은 하나의 점으로 판정한다.

## 평면 교점

평면 판정은 `Intersects`로 bool만 얻거나 `Intersect`로 교점 정보를 얻을 수
있다.

```cpp
using namespace mrg::collision;

Plane3D ground{{0.0F, 1.0F, 0.0F}, 0.0F};
Ray3D mouseRay{{0.0F, 10.0F, 0.0F}, {0.0F, -2.0F, 0.0F}};

if (const auto hit = Intersect(ground, mouseRay))
{
    const DirectX::XMFLOAT3 point = hit->point;
    const DirectX::XMFLOAT3 normalizedNormal = hit->normal;

    // ray: origin + direction * parameter
    const float rayParameter = hit->parameter;
}
```

선분의 `parameter`는 항상 `[0, 1]` 범위다. 직선이나 ray가 평면 위에 완전히
놓인 경우 교점이 무한히 많으므로 입력 시작점을 대표 교점으로 반환하고
`parameter`는 0이 된다.

## OBB 판정

```cpp
using namespace mrg::collision;

Obb2D first{{0.0F, 0.0F}, {2.0F, 1.0F}, 0.25F};
Obb2D second{{2.5F, 0.0F}, {1.0F, 1.0F}, -0.4F};
const bool overlap2D = Intersects(first, second);

Obb3D worldBox{
    {0.0F, 0.0F, 0.0F},
    {1.0F, 2.0F, 1.0F},
    {0.0F, 0.0F, 0.0F, 1.0F}};
Sphere3D player{{1.5F, 0.0F, 0.0F}, 0.75F};
const bool overlap3D = Intersects(player, worldBox);
```

2D OBB 대 OBB는 네 로컬 축을 검사하는 SAT를 사용한다. 3D OBB 판정은
Windows SDK의 DirectXCollision 구현을 사용하되 공개 API에는 해당 구현 타입을
노출하지 않는다.

## 경계와 잘못된 입력

- 서로 접하기만 해도 충돌로 판정한다.
- 기본 허용 오차는 `DefaultEpsilon`이다.
- 각 함수의 마지막 인자로 다른 epsilon을 전달할 수 있다.
- 음수 반지름이나 음수 half extent는 충돌 없음으로 처리한다.
- 길이가 0인 plane normal과 quaternion은 충돌 없음으로 처리한다.
- 판정 함수는 잘못된 기하 입력 때문에 예외를 던지지 않는다.

## 테스트

`Tests/MRG.Collision.Tests.vcxproj`는 다음 항목을 Debug/Release에서 검증한다.

- 평면 교점, 평행·동일 평면 처리, ray 방향 제한
- 원과 직선·선분의 접촉 및 비접촉
- 회전된 2D/3D OBB 판정
- 원/구와 OBB 판정
- 가장 가까운 점과 잘못된 크기·방향 처리

테스트 프로젝트도 기능 헤더가 아닌 `MRG_Core.h`만 포함한다.
