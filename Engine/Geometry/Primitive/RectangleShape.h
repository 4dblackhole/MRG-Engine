#pragma once

#include "Shape/Shape.h"

namespace mrg::geometry
{
    // XY-plane rectangle centered at the origin and facing negative Z.
    class RectangleShape final : public Shape
    {
    public:
        explicit RectangleShape(float width = 2.0F, float height = 2.0F);
    };
}
