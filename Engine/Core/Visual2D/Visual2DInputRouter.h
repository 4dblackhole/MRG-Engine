#pragma once

#include "Visual2D/Visual2DCanvas.h"

namespace mrg::visual2d
{
    struct PointerInput
    {
        Point position{};
        bool available{true};
        bool leftButtonDown{};
        bool leftButtonPressed{};
        bool leftButtonReleased{};
        float wheelDelta{};
        std::int64_t timestampTicks{};
    };

    // Converts pointer snapshots into hover, capture, release and click events.
    class Visual2DInputRouter final
    {
    public:
        void Process(Visual2DCanvas& canvas, const PointerInput& input);
        void Reset(Visual2DCanvas& canvas) noexcept;

        [[nodiscard]] NodeId HoveredNode() const noexcept;
        [[nodiscard]] NodeId CapturedNode() const noexcept;

    private:
        void Dispatch(
            Visual2DCanvas& canvas,
            Visual2DNode& node,
            PointerEventType type,
            const PointerInput& input,
            PointerButton button = PointerButton::None);
        void ChangeHovered(
            Visual2DCanvas& canvas,
            Visual2DNode* next,
            Point nextLocalPosition,
            const PointerInput& input);

        NodeId hovered_{};
        NodeId captured_{};
    };
}
