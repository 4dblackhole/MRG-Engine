#include "Input/UiInputRouter.h"

namespace mrg::ui
{
    void UiInputRouter::Process(
        UiCanvas& canvas,
        const UiPointerInput& input)
    {
        UiElement::HitResult hit{};
        if (input.available)
        {
            hit = canvas.HitTest(input.position);
        }
        ChangeHovered(canvas, hit.element, input);

        if (input.leftButtonPressed && hit.element != nullptr)
        {
            captured_ = hit.element->Id();
            hit.element->SetPressed(true);
            Dispatch(
                canvas,
                *hit.element,
                UiPointerEventType::Press,
                input,
                UiPointerButton::Left);
        }

        UiElement* captured = canvas.FindElement(captured_);
        UiElement* moveTarget = captured != nullptr ? captured : hit.element;
        if (moveTarget != nullptr)
        {
            Dispatch(
                canvas,
                *moveTarget,
                UiPointerEventType::Move,
                input);
        }

        if (input.leftButtonReleased && captured != nullptr)
        {
            Dispatch(
                canvas,
                *captured,
                UiPointerEventType::Release,
                input,
                UiPointerButton::Left);
            captured->SetPressed(false);

            if (hit.element == captured)
            {
                Dispatch(
                    canvas,
                    *captured,
                    UiPointerEventType::Click,
                    input,
                    UiPointerButton::Left);
            }
            captured_ = 0;
        }
        else if (!input.leftButtonDown && !input.leftButtonPressed)
        {
            // Recover safely if focus loss prevented a release event.
            if (captured != nullptr)
            {
                captured->SetPressed(false);
            }
            captured_ = 0;
        }
    }

    void UiInputRouter::Reset(UiCanvas& canvas) noexcept
    {
        if (UiElement* hovered = canvas.FindElement(hovered_))
        {
            hovered->SetHovered(false);
        }
        if (UiElement* captured = canvas.FindElement(captured_))
        {
            captured->SetPressed(false);
        }
        hovered_ = 0;
        captured_ = 0;
    }

    UiElementId UiInputRouter::HoveredElement() const noexcept
    {
        return hovered_;
    }

    UiElementId UiInputRouter::CapturedElement() const noexcept
    {
        return captured_;
    }

    void UiInputRouter::Dispatch(
        UiCanvas& canvas,
        UiElement& element,
        const UiPointerEventType type,
        const UiPointerInput& input,
        const UiPointerButton button)
    {
        const UiRect absoluteBounds = element.BoundsInCanvas();
        element.OnPointerEvent(
            UiPointerEvent{
                type,
                button,
                input.position,
                {input.position.x - absoluteBounds.x,
                    input.position.y - absoluteBounds.y},
                input.timestampTicks},
            canvas.actions_);
    }

    void UiInputRouter::ChangeHovered(
        UiCanvas& canvas,
        UiElement* next,
        const UiPointerInput& input)
    {
        if (next != nullptr && next->Id() == hovered_)
        {
            return;
        }

        if (UiElement* previous = canvas.FindElement(hovered_))
        {
            previous->SetHovered(false);
            Dispatch(canvas, *previous, UiPointerEventType::Leave, input);
        }

        hovered_ = next != nullptr ? next->Id() : 0;
        if (next != nullptr)
        {
            next->SetHovered(true);
            Dispatch(canvas, *next, UiPointerEventType::Enter, input);
        }
    }
}
