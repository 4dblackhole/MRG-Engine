#include "Core/UiElement.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <stdexcept>

namespace mrg::ui
{
    namespace
    {
        std::atomic<UiElementId> nextElementId{1};

        [[nodiscard]] bool IsValidBounds(const UiRect& bounds) noexcept
        {
            return std::isfinite(bounds.x) && std::isfinite(bounds.y) &&
                std::isfinite(bounds.width) && std::isfinite(bounds.height) &&
                bounds.width >= 0.0F && bounds.height >= 0.0F;
        }
    }

    bool UiRect::Contains(const UiPoint point) const noexcept
    {
        return point.x >= x && point.y >= y &&
            point.x <= x + width && point.y <= y + height;
    }

    UiElement::UiElement()
        : id_(nextElementId.fetch_add(1, std::memory_order_relaxed))
    {
    }

    UiElement::~UiElement() = default;

    UiElementId UiElement::Id() const noexcept
    {
        return id_;
    }

    const UiRect& UiElement::Bounds() const noexcept
    {
        return bounds_;
    }

    void UiElement::SetBounds(const UiRect& bounds)
    {
        if (!IsValidBounds(bounds))
        {
            throw std::invalid_argument(
                "UI bounds must be finite and have non-negative size.");
        }
        bounds_ = bounds;
    }

    UiRect UiElement::BoundsInCanvas() const noexcept
    {
        UiRect result = bounds_;
        for (const UiElement* ancestor = parent_;
            ancestor != nullptr;
            ancestor = ancestor->parent_)
        {
            result.x += ancestor->bounds_.x;
            result.y += ancestor->bounds_.y;
        }
        return result;
    }

    bool UiElement::IsVisible() const noexcept
    {
        return visible_;
    }

    void UiElement::SetVisible(const bool visible) noexcept
    {
        visible_ = visible;
        if (!visible_)
        {
            hovered_ = false;
            pressed_ = false;
        }
    }

    bool UiElement::IsEnabled() const noexcept
    {
        return enabled_;
    }

    void UiElement::SetEnabled(const bool enabled) noexcept
    {
        enabled_ = enabled;
        if (!enabled_)
        {
            hovered_ = false;
            pressed_ = false;
        }
    }

    bool UiElement::IsHitTestVisible() const noexcept
    {
        return hitTestVisible_;
    }

    void UiElement::SetHitTestVisible(const bool visible) noexcept
    {
        hitTestVisible_ = visible;
    }

    bool UiElement::IsHovered() const noexcept
    {
        return hovered_;
    }

    bool UiElement::IsPressed() const noexcept
    {
        return pressed_;
    }

    const UiVisualStyle& UiElement::Style() const noexcept
    {
        return style_;
    }

    void UiElement::SetStyle(const UiVisualStyle& style) noexcept
    {
        style_ = style;
    }

    UiElement* UiElement::Parent() noexcept
    {
        return parent_;
    }

    const UiElement* UiElement::Parent() const noexcept
    {
        return parent_;
    }

    const std::vector<std::unique_ptr<UiElement>>& UiElement::Children()
        const noexcept
    {
        return children_;
    }

    UiElement& UiElement::AddChild(std::unique_ptr<UiElement> child)
    {
        if (child == nullptr)
        {
            throw std::invalid_argument("A UI child cannot be null.");
        }
        if (child->parent_ != nullptr)
        {
            throw std::invalid_argument("The UI child already has a parent.");
        }
        child->parent_ = this;
        UiElement& result = *child;
        children_.push_back(std::move(child));
        return result;
    }

    bool UiElement::RemoveChild(const UiElementId id) noexcept
    {
        const auto iterator = std::ranges::find_if(
            children_,
            [id](const std::unique_ptr<UiElement>& child)
            {
                return child->Id() == id;
            });
        if (iterator == children_.end())
        {
            return false;
        }
        (*iterator)->parent_ = nullptr;
        children_.erase(iterator);
        return true;
    }

    UiColor UiElement::CurrentBackgroundColor() const noexcept
    {
        if (!enabled_)
        {
            return style_.disabled;
        }
        if (pressed_)
        {
            return style_.pressed;
        }
        return hovered_ ? style_.hovered : style_.normal;
    }

    void UiElement::AppendDrawCommands(
        std::vector<UiDrawCommand>& commands,
        const UiRect& absoluteBounds) const
    {
        const UiColor color = CurrentBackgroundColor();
        if (color.alpha > 0.0F &&
            absoluteBounds.width > 0.0F && absoluteBounds.height > 0.0F)
        {
            commands.push_back(UiDrawCommand{
                UiDrawCommandType::Rectangle,
                absoluteBounds,
                color});
        }
    }

    void UiElement::OnPointerEvent(
        const UiPointerEvent&,
        std::vector<UiAction>&)
    {
    }

    UiElement::HitResult UiElement::HitTest(
        const UiPoint parentPosition) noexcept
    {
        if (!visible_ || !enabled_ ||
            !bounds_.Contains(parentPosition))
        {
            return {};
        }

        const UiPoint local{
            parentPosition.x - bounds_.x,
            parentPosition.y - bounds_.y};
        for (auto iterator = children_.rbegin();
            iterator != children_.rend(); ++iterator)
        {
            HitResult childHit = (*iterator)->HitTest(local);
            if (childHit.element != nullptr)
            {
                return childHit;
            }
        }

        return hitTestVisible_ ? HitResult{this, local} : HitResult{};
    }

    UiElement* UiElement::Find(const UiElementId id) noexcept
    {
        if (id_ == id)
        {
            return this;
        }
        for (const std::unique_ptr<UiElement>& child : children_)
        {
            if (UiElement* result = child->Find(id); result != nullptr)
            {
                return result;
            }
        }
        return nullptr;
    }

    const UiElement* UiElement::Find(const UiElementId id) const noexcept
    {
        return const_cast<UiElement*>(this)->Find(id);
    }

    void UiElement::CollectDrawCommands(
        std::vector<UiDrawCommand>& commands,
        const UiPoint parentOrigin) const
    {
        if (!visible_)
        {
            return;
        }
        const UiRect absoluteBounds{
            parentOrigin.x + bounds_.x,
            parentOrigin.y + bounds_.y,
            bounds_.width,
            bounds_.height};
        AppendDrawCommands(commands, absoluteBounds);
        const UiPoint childOrigin{absoluteBounds.x, absoluteBounds.y};
        for (const std::unique_ptr<UiElement>& child : children_)
        {
            child->CollectDrawCommands(commands, childOrigin);
        }
    }

    void UiElement::SetHovered(const bool hovered) noexcept
    {
        hovered_ = hovered;
    }

    void UiElement::SetPressed(const bool pressed) noexcept
    {
        pressed_ = pressed;
    }
}
