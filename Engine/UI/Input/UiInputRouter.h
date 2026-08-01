#pragma once

#include "Core/UiCanvas.h"

namespace mrg::ui
{
    struct UiPointerInput
    {
        UiPoint position{};
        bool available{true};
        bool leftButtonDown{};
        bool leftButtonPressed{};
        bool leftButtonReleased{};
        std::int64_t timestampTicks{};
    };

    // Converts pointer snapshots into enter/leave/capture/click semantics.
    // Capture keeps a pressed control as the event target until release.
    class UiInputRouter final
    {
    public:
        void Process(UiCanvas& canvas, const UiPointerInput& input);
        void Reset(UiCanvas& canvas) noexcept;

        [[nodiscard]] UiElementId HoveredElement() const noexcept;
        [[nodiscard]] UiElementId CapturedElement() const noexcept;

    private:
        void Dispatch(
            UiCanvas& canvas,
            UiElement& element,
            UiPointerEventType type,
            const UiPointerInput& input,
            UiPointerButton button = UiPointerButton::None);
        void ChangeHovered(
            UiCanvas& canvas,
            UiElement* next,
            const UiPointerInput& input);

        UiElementId hovered_{};
        UiElementId captured_{};
    };
}
