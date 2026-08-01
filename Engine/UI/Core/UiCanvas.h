#pragma once

#include "Core/UiElement.h"

#include <span>

namespace mrg::ui
{
    class UiCanvas final
    {
    public:
        explicit UiCanvas(UiSize logicalSize);

        [[nodiscard]] UiSize LogicalSize() const noexcept;
        void SetLogicalSize(UiSize logicalSize);
        [[nodiscard]] UiElement& Root() noexcept;
        [[nodiscard]] const UiElement& Root() const noexcept;
        [[nodiscard]] UiElement* FindElement(UiElementId id) noexcept;
        [[nodiscard]] const UiElement* FindElement(UiElementId id) const noexcept;

        [[nodiscard]] std::vector<UiDrawCommand> BuildDrawList() const;
        [[nodiscard]] std::vector<UiAction> TakeActions();

    private:
        friend class UiInputRouter;

        [[nodiscard]] UiElement::HitResult HitTest(UiPoint position) noexcept;

        UiSize logicalSize_{};
        UiElement root_;
        std::vector<UiAction> actions_;
    };
}
