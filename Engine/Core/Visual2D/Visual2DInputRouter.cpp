#include "Visual2D/Visual2DInputRouter.h"

namespace mrg::visual2d
{
    void Visual2DInputRouter::Process(
        Visual2DCanvas& canvas,
        const PointerInput& input)
    {
        Visual2DNode::HitResult hit{};
        if (input.available)
        {
            hit = canvas.HitTest(input.position);
        }
        ChangeHovered(canvas, hit.node, hit.localPosition, input);

        if (input.wheelDelta != 0.0F && hit.node != nullptr)
        {
            Dispatch(canvas, *hit.node, PointerEventType::Wheel, input);
        }
        if (input.leftButtonPressed && hit.node != nullptr)
        {
            captured_ = hit.node->Id();
            hit.node->SetPressed(true);
            Dispatch(
                canvas,
                *hit.node,
                PointerEventType::Press,
                input,
                PointerButton::Left);
        }

        Visual2DNode* captured = canvas.FindNode(captured_);
        Visual2DNode* moveTarget = captured != nullptr ? captured : hit.node;
        if (moveTarget != nullptr)
        {
            Dispatch(canvas, *moveTarget, PointerEventType::Move, input);
        }

        if (input.leftButtonReleased && captured != nullptr)
        {
            Dispatch(
                canvas,
                *captured,
                PointerEventType::Release,
                input,
                PointerButton::Left);
            captured->SetPressed(false);
            if (hit.node == captured)
            {
                Dispatch(
                    canvas,
                    *captured,
                    PointerEventType::Click,
                    input,
                    PointerButton::Left);
            }
            captured_ = 0;
        }
        else if (!input.leftButtonDown && !input.leftButtonPressed)
        {
            if (captured != nullptr)
            {
                captured->SetPressed(false);
            }
            captured_ = 0;
        }
    }

    void Visual2DInputRouter::Reset(Visual2DCanvas& canvas) noexcept
    {
        if (Visual2DNode* hovered = canvas.FindNode(hovered_))
        {
            hovered->SetHovered(false);
        }
        if (Visual2DNode* captured = canvas.FindNode(captured_))
        {
            captured->SetPressed(false);
        }
        hovered_ = 0;
        captured_ = 0;
    }

    NodeId Visual2DInputRouter::HoveredNode() const noexcept
    {
        return hovered_;
    }

    NodeId Visual2DInputRouter::CapturedNode() const noexcept
    {
        return captured_;
    }

    void Visual2DInputRouter::Dispatch(
        Visual2DCanvas& canvas,
        Visual2DNode& node,
        const PointerEventType type,
        const PointerInput& input,
        const PointerButton button)
    {
        Point local{};
        if (!node.MapCanvasPointToLocal(input.position, local))
        {
            local = {};
        }
        node.DispatchPointerEvent(
            PointerEvent{
                type,
                button,
                input.position,
                local,
                input.timestampTicks,
                input.wheelDelta},
            canvas.actions_);
    }

    void Visual2DInputRouter::ChangeHovered(
        Visual2DCanvas& canvas,
        Visual2DNode* next,
        const Point,
        const PointerInput& input)
    {
        if (next != nullptr && next->Id() == hovered_)
        {
            return;
        }
        if (Visual2DNode* previous = canvas.FindNode(hovered_))
        {
            previous->SetHovered(false);
            Dispatch(canvas, *previous, PointerEventType::Leave, input);
        }
        hovered_ = next != nullptr ? next->Id() : 0;
        if (next != nullptr)
        {
            next->SetHovered(true);
            Dispatch(canvas, *next, PointerEventType::Enter, input);
        }
    }
}
