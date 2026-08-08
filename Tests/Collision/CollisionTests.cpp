#include "MRG_Core.h"

#include <DirectXMath.h>

#include <algorithm>
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

        const mrg::geometry::CurvedRectangleShape curvedRectangle(
            3.2F,
            2.1F,
            DirectX::XM_PIDIV4,
            32);
        Check(
            curvedRectangle.VertexCount() == 66 &&
                curvedRectangle.IndexCount() == 192,
            "curved rectangle creates a segmented indexed strip");
        const mrg::ui::MeshUvUiSurface curvedMesh(curvedRectangle, identity);
        const auto curvedHit = curvedMesh.Raycast(centerRay);
        Check(curvedHit.has_value(), "curved UI surface is raycastable");
        if (curvedHit)
        {
            Check(
                NearlyEqual(curvedHit->uv.x, 0.5F) &&
                    NearlyEqual(curvedHit->uv.y, 0.5F),
                "curved surface center maps to center Canvas UV");
        }
    }

    void TestUiRouting()
    {
        mrg::ui::UiCanvas canvas({320.0F, 180.0F});
        auto& button = canvas.Root().EmplaceChild<mrg::ui::UiButton>(L"Apply");
        button.SetBounds({20.0F, 20.0F, 120.0F, 40.0F});

        mrg::ui::UiInputRouter router;
        router.Process(
            canvas,
            {{40.0F, 35.0F}, true, true, true, false, 0.0F, 10});
        Check(button.IsPressed(), "UI button captures a pointer press");
        router.Process(
            canvas,
            {{40.0F, 35.0F}, true, false, false, true, 0.0F, 20});
        const std::vector<mrg::ui::UiAction> actions = canvas.TakeActions();
        Check(!button.IsPressed(), "UI button releases pointer capture");
        Check(
            actions.size() == 1 &&
                actions[0].type == mrg::ui::UiActionType::Clicked &&
                actions[0].source == button.Id(),
            "press and release on one button emits a click");

        auto& slider = canvas.Root().EmplaceChild<mrg::ui::UiSlider>(0.0F);
        slider.SetBounds({20.0F, 80.0F, 200.0F, 40.0F});
        router.Process(
            canvas,
            {{30.0F, 100.0F}, true, true, true, false, 0.0F, 30});
        router.Process(
            canvas,
            {{300.0F, 100.0F}, true, true, false, false, 0.0F, 40});
        Check(
            NearlyEqual(slider.Value(), 1.0F),
            "captured slider keeps receiving movement outside its bounds");
        router.Process(
            canvas,
            {{300.0F, 100.0F}, true, false, false, true, 0.0F, 50});

        auto& image = canvas.Root().EmplaceChild<mrg::ui::UiImage>();
        image.SetImage(mrg::ui::UiImageHandle{123});
        image.SetBounds({240.0F, 20.0F, 40.0F, 40.0F});
        const std::vector<mrg::ui::UiDrawCommand> imageCommands =
            canvas.BuildDrawList();
        const bool imageCommandFound = std::any_of(
            imageCommands.begin(),
            imageCommands.end(),
            [](const mrg::ui::UiDrawCommand& command)
            {
                return command.type == mrg::ui::UiDrawCommandType::Image &&
                    command.image.value == 123;
            });
        Check(
            imageCommandFound,
            "UI image widgets emit reusable image draw commands");

        auto& combo = canvas.Root().EmplaceChild<mrg::ui::UiComboBox>();
        combo.SetBounds({20.0F, 130.0F, 180.0F, 30.0F});
        combo.SetItemHeight(20.0F);
        combo.SetMaxVisibleItems(3);
        combo.SetItems({L"Driver 0", L"Driver 1", L"Driver 2", L"Driver 3", L"Driver 4"});

        // A click on the field opens its popup. The popup extends outside the
        // canvas, so this also checks that it is not clipped by the root bounds.
        router.Process(
            canvas,
            {{50.0F, 145.0F}, true, true, true, false, 0.0F, 60});
        router.Process(
            canvas,
            {{50.0F, 145.0F}, true, false, false, true, 0.0F, 70});
        Check(combo.IsExpanded(), "combo box opens a popup from the field click");

        // Moving the pointer to an unrelated Canvas area must not dismiss the
        // device list. The player may return to the popup and continue input.
        router.Process(
            canvas,
            {{300.0F, 20.0F}, true, false, false, false, 0.0F, 75});
        Check(
            combo.IsExpanded(),
            "combo box remains open after losing pointer focus");

        // One normalized wheel tick advances the first visible row by one.
        router.Process(
            canvas,
            {{50.0F, 180.0F}, true, false, false, false, -1.0F, 80});
        router.Process(
            canvas,
            {{50.0F, 180.0F}, true, true, true, false, 0.0F, 90});
        router.Process(
            canvas,
            {{50.0F, 180.0F}, true, false, false, true, 0.0F, 100});
        const std::vector<mrg::ui::UiAction> comboActions = canvas.TakeActions();
        Check(
            combo.SelectedIndex() == 2 &&
                !comboActions.empty() &&
                comboActions.back().type == mrg::ui::UiActionType::SelectionChanged,
            "combo box wheel scrolling selects the shifted visible item");

        // Reopen and drag upward by one row before selecting the second row.
        router.Process(
            canvas,
            {{50.0F, 145.0F}, true, true, true, false, 0.0F, 110});
        router.Process(
            canvas,
            {{50.0F, 145.0F}, true, false, false, true, 0.0F, 120});
        router.Process(
            canvas,
            {{50.0F, 200.0F}, true, true, true, false, 0.0F, 130});
        router.Process(
            canvas,
            {{50.0F, 180.0F}, true, true, false, false, 0.0F, 140});
        router.Process(
            canvas,
            {{50.0F, 180.0F}, true, false, false, true, 0.0F, 150});
        router.Process(
            canvas,
            {{50.0F, 190.0F}, true, true, true, false, 0.0F, 160});
        router.Process(
            canvas,
            {{50.0F, 190.0F}, true, false, false, true, 0.0F, 170});
        Check(
            combo.SelectedIndex() == 3,
            "combo box drag scrolling selects the shifted visible item");
    }

    void TestUiTreeZOrder()
    {
        mrg::ui::UiCanvas canvas({240.0F, 140.0F});
        auto& front = canvas.Root().EmplaceChild<mrg::ui::UiButton>(L"Front");
        front.SetBounds({40.0F, 40.0F, 100.0F, 48.0F});
        front.SetZIndex(10);
        auto& back = canvas.Root().EmplaceChild<mrg::ui::UiButton>(L"Back");
        back.SetBounds({20.0F, 20.0F, 180.0F, 100.0F});

        const std::vector<mrg::ui::UiDrawCommand> commands =
            canvas.BuildDrawList();
        Check(
            !commands.empty() && commands.back().text == L"Front",
            "larger sibling Z-index paints its complete subtree last");

        mrg::ui::UiInputRouter router;
        router.Process(
            canvas,
            {{60.0F, 60.0F}, true, true, true, false, 0.0F, 10});
        router.Process(
            canvas,
            {{60.0F, 60.0F}, true, false, false, true, 0.0F, 20});
        std::vector<mrg::ui::UiAction> actions = canvas.TakeActions();
        Check(
            actions.size() == 1 && actions.front().source == front.Id(),
            "hit testing uses the reverse of sibling paint order");

        // Outside the visible front rectangle, the same point must no longer
        // target it even though the lower sibling remains underneath.
        router.Process(
            canvas,
            {{30.0F, 30.0F}, true, true, true, false, 0.0F, 30});
        router.Process(
            canvas,
            {{30.0F, 30.0F}, true, false, false, true, 0.0F, 40});
        actions = canvas.TakeActions();
        Check(
            actions.size() == 1 && actions.front().source == back.Id(),
            "a widget hit box matches its rendered bounds");
    }
}

int main()
{
    TestPlaneAndLines();
    TestTwoDimensionalQueries();
    TestThreeDimensionalVolumes();
    TestTriangleAndUvSurfaces();
    TestUiRouting();
    TestUiTreeZOrder();

    if (failureCount != 0)
    {
        std::cerr << failureCount << " engine test(s) failed.\n";
        return 1;
    }

    std::cout << "All collision and UI tests passed.\n";
    return 0;
}
