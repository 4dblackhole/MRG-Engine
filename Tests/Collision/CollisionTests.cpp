#include "MRG_Core.h"

#include <DirectXMath.h>

#include <cmath>
#include <iostream>
#include <string_view>

namespace
{
    int failureCount = 0;

    void Check(const bool condition, const std::string_view description)
    {
        if (!condition)
        {
            ++failureCount;
            std::cerr << "FAILED: " << description << '\n';
        }
    }

    [[nodiscard]] bool NearlyEqual(
        const float first,
        const float second,
        const float epsilon = 1.0e-4F) noexcept
    {
        return std::abs(first - second) <= epsilon;
    }

    void TestPlaneAndLines()
    {
        using namespace mrg::collision;

        const Plane3D ground{{0.0F, 2.0F, 0.0F}, 0.0F};
        const Line3D descending{{0.0F, 2.0F, 0.0F}, {0.0F, -2.0F, 0.0F}};
        const auto lineHit = Intersect(ground, descending);
        Check(lineHit.has_value(), "plane intersects an infinite line");
        if (lineHit)
        {
            Check(NearlyEqual(lineHit->parameter, 1.0F), "line parameter");
            Check(NearlyEqual(lineHit->point.y, 0.0F), "line hit position");
            Check(NearlyEqual(lineHit->normal.y, 1.0F), "plane normal normalized");
        }

        Check(
            !Intersects(
                ground,
                Line3D{{0.0F, 2.0F, 0.0F}, {1.0F, 0.0F, 0.0F}}),
            "separate parallel line does not intersect plane");
        Check(
            Intersects(
                ground,
                Line3D{{0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}}),
            "coplanar line intersects plane");
        Check(
            !Intersects(
                ground,
                Ray3D{{0.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}}),
            "ray pointing away from plane does not intersect");

        const auto segmentHit = Intersect(
            ground,
            LineSegment3D{{0.0F, -1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}});
        Check(segmentHit.has_value(), "plane intersects finite segment");
        if (segmentHit)
        {
            Check(
                NearlyEqual(segmentHit->parameter, 0.5F),
                "segment parameter is normalized");
        }
        Check(
            Intersects(
                ground,
                LineSegment3D{{1.0F, 0.0F, 1.0F}, {1.0F, 0.0F, 1.0F}}),
            "degenerate segment on plane is a point intersection");
    }

    void TestTwoDimensionalQueries()
    {
        using namespace mrg::collision;

        const Circle2D circle{{0.0F, 1.0F}, 1.0F};
        Check(
            Intersects(circle, Line2D{{0.0F, 0.0F}, {2.0F, 0.0F}}),
            "circle tangent to infinite line");
        Check(
            !Intersects(circle, Line2D{{0.0F, -1.0F}, {1.0F, 0.0F}}),
            "circle separate from infinite line");
        Check(
            Intersects(
                circle,
                LineSegment2D{{-2.0F, 0.0F}, {2.0F, 0.0F}}),
            "circle tangent to segment");
        Check(
            !Intersects(
                circle,
                LineSegment2D{{2.0F, 0.0F}, {3.0F, 0.0F}}),
            "circle does not reach finite segment");

        const Obb2D rotatedBox{
            {0.0F, 0.0F},
            {2.0F, 0.5F},
            DirectX::XM_PIDIV4};
        Check(
            Intersects(Circle2D{{1.2F, 1.2F}, 0.25F}, rotatedBox),
            "circle intersects rotated OBB");
        Check(
            !Intersects(Circle2D{{4.0F, 4.0F}, 0.5F}, rotatedBox),
            "circle is separate from rotated OBB");

        const Obb2D overlapping{
            {1.0F, 0.0F},
            {1.0F, 1.0F},
            -DirectX::XM_PIDIV4};
        const Obb2D separate{
            {6.0F, 0.0F},
            {1.0F, 1.0F},
            DirectX::XM_PIDIV4};
        Check(Intersects(rotatedBox, overlapping), "two OBBs overlap");
        Check(!Intersects(rotatedBox, separate), "two OBBs are separate");

        const DirectX::XMFLOAT2 closest = ClosestPoint(
            Obb2D{{0.0F, 0.0F}, {1.0F, 2.0F}, 0.0F},
            {4.0F, 3.0F});
        Check(
            NearlyEqual(closest.x, 1.0F) && NearlyEqual(closest.y, 2.0F),
            "closest point on 2D OBB");

        Check(
            !Intersects(
                Circle2D{{0.0F, 0.0F}, -1.0F},
                Line2D{{}, {1.0F, 0.0F}}),
            "negative circle radius is rejected");
        Check(
            !Intersects(
                Circle2D{{0.0F, 0.0F}, 1.0F},
                Line2D{{}, {0.0F, 0.0F}}),
            "zero line direction is rejected");
    }

    void TestThreeDimensionalVolumes()
    {
        using namespace mrg::collision;

        const Obb3D first{
            {0.0F, 0.0F, 0.0F},
            {1.0F, 1.0F, 1.0F},
            {0.0F, 0.0F, 0.0F, 1.0F}};
        const float halfAngle = DirectX::XM_PIDIV4 * 0.5F;
        const Obb3D rotated{
            {1.4F, 0.0F, 0.0F},
            {1.0F, 0.5F, 1.0F},
            {0.0F, std::sin(halfAngle), 0.0F, std::cos(halfAngle)}};
        const Obb3D separate{
            {5.0F, 0.0F, 0.0F},
            {1.0F, 1.0F, 1.0F},
            {0.0F, 0.0F, 0.0F, 1.0F}};

        Check(Intersects(first, rotated), "two 3D OBBs overlap");
        Check(!Intersects(first, separate), "two 3D OBBs are separate");

        const Sphere3D sphere{{0.0F, 0.0F, 0.0F}, 1.0F};
        Check(
            Intersects(
                sphere,
                Line3D{{-2.0F, 1.0F, 0.0F}, {1.0F, 0.0F, 0.0F}}),
            "sphere tangent to infinite line");
        Check(
            !Intersects(
                sphere,
                Ray3D{{2.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}}),
            "ray points away from sphere");
        Check(
            Intersects(
                sphere,
                LineSegment3D{{-2.0F, 0.0F, 0.0F}, {2.0F, 0.0F, 0.0F}}),
            "sphere intersects finite segment");
        Check(Intersects(sphere, first), "sphere intersects 3D OBB");
        Check(
            !Intersects(Sphere3D{{4.0F, 4.0F, 4.0F}, 0.5F}, first),
            "sphere is separate from 3D OBB");
    }

    void TestTriangleAndUvSurfaces()
    {
        using namespace mrg::collision;

        const Triangle3D triangle{
            {-1.0F, -1.0F, 0.0F},
            {-1.0F, 1.0F, 0.0F},
            {1.0F, 1.0F, 0.0F}};
        const Ray3D centerRay{{0.0F, 0.0F, -2.0F}, {0.0F, 0.0F, 1.0F}};
        const auto triangleHit = Intersect(triangle, centerRay);
        Check(triangleHit.has_value(), "ray intersects an indexed UI triangle");
        if (triangleHit)
        {
            const float weightSum = triangleHit->barycentric.x +
                triangleHit->barycentric.y + triangleHit->barycentric.z;
            Check(NearlyEqual(weightSum, 1.0F), "triangle barycentric weights");
            Check(NearlyEqual(triangleHit->parameter, 2.0F), "triangle ray parameter");
        }

        DirectX::XMFLOAT4X4 identity{};
        DirectX::XMStoreFloat4x4(&identity, DirectX::XMMatrixIdentity());
        const mrg::ui::PlaneUiSurface plane(2.0F, 2.0F, identity);
        const auto planeHit = plane.Raycast(centerRay);
        Check(planeHit.has_value(), "world-space UI plane is raycastable");
        if (planeHit)
        {
            Check(
                NearlyEqual(planeHit->uv.x, 0.5F) &&
                    NearlyEqual(planeHit->uv.y, 0.5F),
                "plane center maps to center UV");
        }

        const mrg::geometry::RectangleShape rectangle(2.0F, 2.0F);
        const mrg::ui::MeshUvUiSurface mesh(rectangle, identity);
        const auto meshHit = mesh.Raycast(centerRay);
        Check(meshHit.has_value(), "mesh UV UI surface is raycastable");
        if (meshHit)
        {
            Check(
                NearlyEqual(meshHit->uv.x, 0.5F) &&
                    NearlyEqual(meshHit->uv.y, 0.5F),
                "barycentric interpolation preserves rectangle UV");
        }
    }

    void TestUiRouting()
    {
        mrg::ui::UiCanvas canvas({320.0F, 180.0F});
        auto& button = canvas.Root().EmplaceChild<mrg::ui::UiButton>(L"Apply");
        button.SetBounds({20.0F, 20.0F, 120.0F, 40.0F});

        mrg::ui::UiInputRouter router;
        router.Process(canvas, {{40.0F, 35.0F}, true, true, true, false, 10});
        Check(button.IsPressed(), "UI button captures a pointer press");
        router.Process(canvas, {{40.0F, 35.0F}, true, false, false, true, 20});
        const std::vector<mrg::ui::UiAction> actions = canvas.TakeActions();
        Check(!button.IsPressed(), "UI button releases pointer capture");
        Check(
            actions.size() == 1 &&
                actions[0].type == mrg::ui::UiActionType::Clicked &&
                actions[0].source == button.Id(),
            "press and release on one button emits a click");

        auto& slider = canvas.Root().EmplaceChild<mrg::ui::UiSlider>(0.0F);
        slider.SetBounds({20.0F, 80.0F, 200.0F, 40.0F});
        router.Process(canvas, {{30.0F, 100.0F}, true, true, true, false, 30});
        router.Process(canvas, {{300.0F, 100.0F}, true, true, false, false, 40});
        Check(
            NearlyEqual(slider.Value(), 1.0F),
            "captured slider keeps receiving movement outside its bounds");
        router.Process(canvas, {{300.0F, 100.0F}, true, false, false, true, 50});
    }
}

int main()
{
    TestPlaneAndLines();
    TestTwoDimensionalQueries();
    TestThreeDimensionalVolumes();
    TestTriangleAndUvSurfaces();
    TestUiRouting();

    if (failureCount != 0)
    {
        std::cerr << failureCount << " engine test(s) failed.\n";
        return 1;
    }

    std::cout << "All collision and UI tests passed.\n";
    return 0;
}
