#pragma once

#include "Shape/Shape.h"

#include <cstdint>

namespace mrg::geometry
{
    // A vertically straight, horizontally curved rectangle centered at the
    // origin. UVs span the complete surface, so it can host a rendered canvas.
    class CurvedRectangleShape final : public Shape
    {
    public:
        CurvedRectangleShape(
            float width = 2.0F,
            float height = 2.0F,
            float curvatureRadians = 0.785398163F,
            std::uint32_t horizontalSegments = 32);
    };
}
