#include "Core/UiCanvas.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace mrg::ui
{
    namespace
    {
        void ValidateSize(const UiSize size)
        {
            if (!std::isfinite(size.width) || !std::isfinite(size.height) ||
                size.width <= 0.0F || size.height <= 0.0F)
            {
                throw std::invalid_argument(
                    "A UI canvas requires a finite positive logical size.");
            }
        }
    }

    UiCanvas::UiCanvas(const UiSize logicalSize)
        : logicalSize_(logicalSize)
    {
        ValidateSize(logicalSize_);
        root_.SetBounds({0.0F, 0.0F, logicalSize.width, logicalSize.height});
        root_.SetHitTestVisible(false);
        UiVisualStyle transparent{};
        transparent.normal.alpha = 0.0F;
        transparent.hovered.alpha = 0.0F;
        transparent.pressed.alpha = 0.0F;
        transparent.disabled.alpha = 0.0F;
        root_.SetStyle(transparent);
    }

    UiSize UiCanvas::LogicalSize() const noexcept
    {
        return logicalSize_;
    }

    void UiCanvas::SetLogicalSize(const UiSize logicalSize)
    {
        ValidateSize(logicalSize);
        logicalSize_ = logicalSize;
        root_.SetBounds({0.0F, 0.0F, logicalSize.width, logicalSize.height});
    }

    UiElement& UiCanvas::Root() noexcept
    {
        return root_;
    }

    const UiElement& UiCanvas::Root() const noexcept
    {
        return root_;
    }

    UiElement* UiCanvas::FindElement(const UiElementId id) noexcept
    {
        return root_.Find(id);
    }

    const UiElement* UiCanvas::FindElement(const UiElementId id) const noexcept
    {
        return root_.Find(id);
    }

    std::vector<UiDrawCommand> UiCanvas::BuildDrawList() const
    {
        std::vector<UiDrawCommand> commands;
        root_.CollectDrawCommands(commands, {});
        return commands;
    }

    std::vector<UiAction> UiCanvas::TakeActions()
    {
        std::vector<UiAction> result = std::move(actions_);
        actions_.clear();
        return result;
    }

    UiElement::HitResult UiCanvas::HitTest(const UiPoint position) noexcept
    {
        return root_.HitTest(position);
    }
}
