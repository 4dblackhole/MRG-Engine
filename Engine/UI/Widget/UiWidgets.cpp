#include "Widget/UiWidgets.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace mrg::ui
{
    namespace
    {
        [[nodiscard]] UiDrawCommand MakeTextCommand(
            const UiRect bounds,
            const UiColor color,
            const std::wstring_view text,
            const float fontSize,
            const UiTextAlignment alignment)
        {
            UiDrawCommand command{};
            command.type = UiDrawCommandType::Text;
            command.bounds = bounds;
            command.color = color;
            command.text = text;
            command.fontSize = fontSize;
            command.horizontalAlignment = alignment;
            return command;
        }

        constexpr std::size_t NoItemIndex = static_cast<std::size_t>(-1);
    }

    UiLabel::UiLabel(std::wstring text)
        : text_(std::move(text))
    {
        SetHitTestVisible(false);
        UiVisualStyle transparent{};
        transparent.normal.alpha = 0.0F;
        transparent.hovered.alpha = 0.0F;
        transparent.pressed.alpha = 0.0F;
        transparent.disabled.alpha = 0.0F;
        SetStyle(transparent);
    }

    std::wstring_view UiLabel::Text() const noexcept
    {
        return text_;
    }

    void UiLabel::SetText(std::wstring text)
    {
        text_ = std::move(text);
    }

    float UiLabel::FontSize() const noexcept
    {
        return fontSize_;
    }

    void UiLabel::SetFontSize(const float fontSize)
    {
        if (!std::isfinite(fontSize) || fontSize <= 0.0F)
        {
            throw std::invalid_argument("UI font size must be positive.");
        }
        fontSize_ = fontSize;
    }

    UiColor UiLabel::TextColor() const noexcept
    {
        return textColor_;
    }

    void UiLabel::SetTextColor(const UiColor color) noexcept
    {
        textColor_ = color;
    }

    void UiLabel::SetHorizontalAlignment(
        const UiTextAlignment alignment) noexcept
    {
        alignment_ = alignment;
    }

    void UiLabel::AppendDrawCommands(
        std::vector<UiDrawCommand>& commands,
        const UiRect& absoluteBounds) const
    {
        UiElement::AppendDrawCommands(commands, absoluteBounds);
        if (!text_.empty())
        {
            commands.push_back(MakeTextCommand(
                absoluteBounds,
                textColor_,
                text_,
                fontSize_,
                alignment_));
        }
    }

    UiImageHandle UiImage::Image() const noexcept
    {
        return image_;
    }

    void UiImage::SetImage(const UiImageHandle image) noexcept
    {
        image_ = image;
    }

    UiColor UiImage::Tint() const noexcept
    {
        return tint_;
    }

    void UiImage::SetTint(const UiColor tint) noexcept
    {
        tint_ = tint;
    }

    void UiImage::AppendDrawCommands(
        std::vector<UiDrawCommand>& commands,
        const UiRect& absoluteBounds) const
    {
        if (!image_ || absoluteBounds.width <= 0.0F ||
            absoluteBounds.height <= 0.0F)
        {
            return;
        }

        UiDrawCommand command{};
        command.type = UiDrawCommandType::Image;
        command.bounds = absoluteBounds;
        command.color = tint_;
        command.image = image_;
        commands.push_back(std::move(command));
    }

    UiButton::UiButton(std::wstring text)
        : text_(std::move(text))
    {
    }

    std::wstring_view UiButton::Text() const noexcept
    {
        return text_;
    }

    void UiButton::SetText(std::wstring text)
    {
        text_ = std::move(text);
    }

    void UiButton::SetFontSize(const float fontSize)
    {
        if (!std::isfinite(fontSize) || fontSize <= 0.0F)
        {
            throw std::invalid_argument("UI font size must be positive.");
        }
        fontSize_ = fontSize;
    }

    void UiButton::AppendDrawCommands(
        std::vector<UiDrawCommand>& commands,
        const UiRect& absoluteBounds) const
    {
        UiElement::AppendDrawCommands(commands, absoluteBounds);
        if (!text_.empty())
        {
            commands.push_back(MakeTextCommand(
                absoluteBounds,
                {1.0F, 1.0F, 1.0F, IsEnabled() ? 1.0F : 0.55F},
                text_,
                fontSize_,
                UiTextAlignment::Center));
        }
    }

    void UiButton::OnPointerEvent(
        const UiPointerEvent& event,
        std::vector<UiAction>& actions)
    {
        if (event.type == UiPointerEventType::Click)
        {
            actions.push_back(UiAction{
                UiActionType::Clicked,
                Id(),
                0.0F,
                0,
                event.timestampTicks});
        }
    }

    float UiButton::FontSize() const noexcept
    {
        return fontSize_;
    }

    UiToggle::UiToggle(std::wstring text, const bool checked)
        : UiButton(std::move(text)), checked_(checked)
    {
    }

    bool UiToggle::IsChecked() const noexcept
    {
        return checked_;
    }

    void UiToggle::SetChecked(const bool checked) noexcept
    {
        checked_ = checked;
    }

    void UiToggle::AppendDrawCommands(
        std::vector<UiDrawCommand>& commands,
        const UiRect& absoluteBounds) const
    {
        UiButton::AppendDrawCommands(commands, absoluteBounds);
        const float indicatorSize = std::min(absoluteBounds.height * 0.5F, 18.0F);
        const UiRect indicator{
            absoluteBounds.x + 10.0F,
            absoluteBounds.y + (absoluteBounds.height - indicatorSize) * 0.5F,
            indicatorSize,
            indicatorSize};
        commands.push_back(UiDrawCommand{
            UiDrawCommandType::Rectangle,
            indicator,
            checked_
                ? UiColor{0.20F, 0.85F, 0.48F, 1.0F}
                : UiColor{0.08F, 0.09F, 0.12F, 1.0F}});
    }

    void UiToggle::OnPointerEvent(
        const UiPointerEvent& event,
        std::vector<UiAction>& actions)
    {
        if (event.type != UiPointerEventType::Click)
        {
            return;
        }
        checked_ = !checked_;
        actions.push_back(UiAction{
            UiActionType::ValueChanged,
            Id(),
            checked_ ? 1.0F : 0.0F,
            0,
            event.timestampTicks});
    }

    UiSlider::UiSlider(const float value)
    {
        SetValue(value);
    }

    float UiSlider::Value() const noexcept
    {
        return value_;
    }

    void UiSlider::SetValue(const float value) noexcept
    {
        value_ = std::clamp(value, 0.0F, 1.0F);
    }

    void UiSlider::AppendDrawCommands(
        std::vector<UiDrawCommand>& commands,
        const UiRect& absoluteBounds) const
    {
        UiElement::AppendDrawCommands(commands, absoluteBounds);
        const float trackHeight = std::min(6.0F, absoluteBounds.height);
        const UiRect track{
            absoluteBounds.x + 8.0F,
            absoluteBounds.y + (absoluteBounds.height - trackHeight) * 0.5F,
            std::max(0.0F, absoluteBounds.width - 16.0F),
            trackHeight};
        commands.push_back(UiDrawCommand{
            UiDrawCommandType::Rectangle,
            track,
            {0.08F, 0.09F, 0.12F, 1.0F}});
        const float thumbWidth = 12.0F;
        commands.push_back(UiDrawCommand{
            UiDrawCommandType::Rectangle,
            {track.x + track.width * value_ - thumbWidth * 0.5F,
                absoluteBounds.y + 5.0F,
                thumbWidth,
                std::max(0.0F, absoluteBounds.height - 10.0F)},
            {0.30F, 0.70F, 1.0F, 1.0F}});
    }

    void UiSlider::OnPointerEvent(
        const UiPointerEvent& event,
        std::vector<UiAction>& actions)
    {
        if (event.type == UiPointerEventType::Press ||
            (event.type == UiPointerEventType::Move && IsPressed()))
        {
            UpdateFromPointer(event, actions);
        }
    }

    void UiSlider::UpdateFromPointer(
        const UiPointerEvent& event,
        std::vector<UiAction>& actions)
    {
        const float usableWidth = std::max(Bounds().width - 16.0F, 1.0F);
        const float updated = std::clamp(
            (event.localPosition.x - 8.0F) / usableWidth,
            0.0F,
            1.0F);
        if (updated == value_)
        {
            return;
        }
        value_ = updated;
        actions.push_back(UiAction{
            UiActionType::ValueChanged,
            Id(),
            value_,
            0,
            event.timestampTicks});
    }

    void UiCycleSelector::SetItems(std::vector<std::wstring> items)
    {
        items_ = std::move(items);
        if (selectedIndex_ >= items_.size())
        {
            selectedIndex_ = 0;
        }
        RefreshText();
    }

    const std::vector<std::wstring>& UiCycleSelector::Items() const noexcept
    {
        return items_;
    }

    std::size_t UiCycleSelector::SelectedIndex() const noexcept
    {
        return selectedIndex_;
    }

    void UiCycleSelector::SetSelectedIndex(const std::size_t index)
    {
        if (index >= items_.size())
        {
            throw std::out_of_range("UI cycle-selector selection is out of range.");
        }
        selectedIndex_ = index;
        RefreshText();
    }

    void UiCycleSelector::OnPointerEvent(
        const UiPointerEvent& event,
        std::vector<UiAction>& actions)
    {
        if (event.type != UiPointerEventType::Click || items_.empty())
        {
            return;
        }
        selectedIndex_ = (selectedIndex_ + 1) % items_.size();
        RefreshText();
        actions.push_back(UiAction{
            UiActionType::SelectionChanged,
            Id(),
            static_cast<float>(selectedIndex_),
            selectedIndex_,
            event.timestampTicks});
    }

    void UiCycleSelector::RefreshText()
    {
        SetText(items_.empty() ? std::wstring{} : items_[selectedIndex_]);
    }

    void UiComboBox::SetItems(std::vector<std::wstring> items)
    {
        items_ = std::move(items);
        if (selectedIndex_ >= items_.size())
        {
            selectedIndex_ = 0;
        }
        firstVisibleIndex_ = 0;
        hoveredItemIndex_ = NoItemIndex;
        if (items_.empty())
        {
            Collapse();
        }
        RefreshText();
    }

    const std::vector<std::wstring>& UiComboBox::Items() const noexcept
    {
        return items_;
    }

    std::size_t UiComboBox::SelectedIndex() const noexcept
    {
        return selectedIndex_;
    }

    void UiComboBox::SetSelectedIndex(const std::size_t index)
    {
        if (index >= items_.size())
        {
            throw std::out_of_range("UI combo-box selection is out of range.");
        }
        selectedIndex_ = index;
        EnsureSelectedItemVisible();
        RefreshText();
    }

    void UiComboBox::SetMaxVisibleItems(const std::size_t maxVisibleItems)
    {
        if (maxVisibleItems == 0)
        {
            throw std::invalid_argument(
                "A UI combo box must show at least one item.");
        }
        maxVisibleItems_ = maxVisibleItems;
        EnsureSelectedItemVisible();
    }

    std::size_t UiComboBox::MaxVisibleItems() const noexcept
    {
        return maxVisibleItems_;
    }

    void UiComboBox::SetItemHeight(const float itemHeight)
    {
        if (!std::isfinite(itemHeight) || itemHeight <= 0.0F)
        {
            throw std::invalid_argument(
                "A UI combo-box item height must be positive.");
        }
        itemHeight_ = itemHeight;
    }

    float UiComboBox::ItemHeight() const noexcept
    {
        return itemHeight_;
    }

    bool UiComboBox::IsExpanded() const noexcept
    {
        return expanded_;
    }

    void UiComboBox::Collapse() noexcept
    {
        expanded_ = false;
        trackingDrag_ = false;
        dragMoved_ = false;
        hoveredItemIndex_ = NoItemIndex;
    }

    void UiComboBox::AppendDrawCommands(
        std::vector<UiDrawCommand>& commands,
        const UiRect& absoluteBounds) const
    {
        // Keep the displayed field and its hit rectangle identical. Text uses
        // a padded content region so long device names never run underneath
        // the arrow affordance on the right.
        UiElement::AppendDrawCommands(commands, absoluteBounds);
        const float arrowWidth = std::min(34.0F, absoluteBounds.width);
        if (!Text().empty())
        {
            commands.push_back(MakeTextCommand(
                {absoluteBounds.x + 12.0F,
                    absoluteBounds.y,
                    std::max(
                        absoluteBounds.width - arrowWidth - 18.0F,
                        0.0F),
                    absoluteBounds.height},
                {1.0F, 1.0F, 1.0F, IsEnabled() ? 1.0F : 0.55F},
                Text(),
                FontSize(),
                UiTextAlignment::Leading));
        }
        commands.push_back(UiDrawCommand{
            UiDrawCommandType::Rectangle,
            {absoluteBounds.x + absoluteBounds.width - arrowWidth,
                absoluteBounds.y,
                arrowWidth,
                absoluteBounds.height},
            {0.025F, 0.055F, 0.125F, IsEnabled() ? 0.92F : 0.50F}});
        commands.push_back(MakeTextCommand(
            {absoluteBounds.x + absoluteBounds.width - arrowWidth,
                absoluteBounds.y,
                arrowWidth,
                absoluteBounds.height},
            {0.72F, 0.84F, 1.0F, IsEnabled() ? 1.0F : 0.55F},
            expanded_ ? L"▲" : L"▼",
            std::min(FontSize(), 14.0F),
            UiTextAlignment::Center));

        if (!expanded_ || items_.empty())
        {
            return;
        }

        const UiRect localPopup = PopupBounds();
        const UiRect popup{
            absoluteBounds.x + localPopup.x,
            absoluteBounds.y + localPopup.y,
            localPopup.width,
            localPopup.height};
        UiDrawCommand background{};
        background.type = UiDrawCommandType::Rectangle;
        background.bounds = popup;
        background.color = {0.055F, 0.070F, 0.105F, 0.98F};
        commands.push_back(std::move(background));

        const std::size_t visibleCount = VisibleItemCount();
        for (std::size_t row = 0; row < visibleCount; ++row)
        {
            const std::size_t itemIndex = firstVisibleIndex_ + row;
            const UiRect itemBounds{
                popup.x,
                popup.y + static_cast<float>(row) * itemHeight_,
                popup.width,
                itemHeight_};
            const UiColor itemColor = itemIndex == hoveredItemIndex_
                ? Style().hovered
                : itemIndex == selectedIndex_
                    ? Style().pressed
                    : Style().normal;
            UiDrawCommand itemBackground{};
            itemBackground.type = UiDrawCommandType::Rectangle;
            itemBackground.bounds = itemBounds;
            itemBackground.color = itemColor;
            commands.push_back(std::move(itemBackground));
            commands.push_back(MakeTextCommand(
                {itemBounds.x + 10.0F,
                    itemBounds.y,
                    std::max(itemBounds.width - 20.0F, 0.0F),
                    itemBounds.height},
                {1.0F, 1.0F, 1.0F, 1.0F},
                items_[itemIndex],
                FontSize(),
                UiTextAlignment::Leading));
        }

        if (items_.size() > visibleCount && popup.height > 0.0F)
        {
            const float trackWidth = 6.0F;
            const float thumbHeight = std::max(
                12.0F,
                popup.height * static_cast<float>(visibleCount) /
                    static_cast<float>(items_.size()));
            const std::size_t maxFirstIndex = items_.size() - visibleCount;
            const float progress = maxFirstIndex == 0
                ? 0.0F
                : static_cast<float>(firstVisibleIndex_) /
                    static_cast<float>(maxFirstIndex);
            UiDrawCommand scrollTrack{};
            scrollTrack.type = UiDrawCommandType::Rectangle;
            scrollTrack.bounds = {
                popup.x + popup.width - trackWidth,
                popup.y,
                trackWidth,
                popup.height};
            scrollTrack.color = {0.02F, 0.025F, 0.04F, 0.90F};
            commands.push_back(std::move(scrollTrack));
            UiDrawCommand scrollThumb{};
            scrollThumb.type = UiDrawCommandType::Rectangle;
            scrollThumb.bounds = {
                popup.x + popup.width - trackWidth,
                popup.y + (popup.height - thumbHeight) * progress,
                trackWidth,
                thumbHeight};
            scrollThumb.color = {0.35F, 0.70F, 1.0F, 1.0F};
            commands.push_back(std::move(scrollThumb));
        }
    }

    void UiComboBox::OnPointerEvent(
        const UiPointerEvent& event,
        std::vector<UiAction>& actions)
    {
        if (items_.empty())
        {
            return;
        }

        if (event.type == UiPointerEventType::Enter ||
            event.type == UiPointerEventType::Move)
        {
            if (expanded_)
            {
                UpdateHoveredItem(event.localPosition);
            }
            if (event.type == UiPointerEventType::Move && trackingDrag_ &&
                IsPressed())
            {
                const float displacement = dragStartY_ - event.localPosition.y;
                if (std::abs(displacement) > 3.0F)
                {
                    const int itemDelta = static_cast<int>(
                        std::trunc(displacement / itemHeight_));
                    ScrollBy(static_cast<int>(dragStartFirstVisibleIndex_) +
                        itemDelta - static_cast<int>(firstVisibleIndex_));
                    dragMoved_ = true;
                }
            }
            return;
        }

        if (event.type == UiPointerEventType::Leave)
        {
            hoveredItemIndex_ = NoItemIndex;
            // Pointer focus is only a visual state. Keep the popup open so a
            // player can move away temporarily and return without losing the
            // device list; selection and an explicit field click still close it.
            return;
        }

        if (event.type == UiPointerEventType::Wheel && expanded_ &&
            PopupBounds().Contains(event.localPosition))
        {
            const int direction = event.wheelDelta > 0.0F ? -1 : 1;
            const int stepCount = std::max(
                1,
                static_cast<int>(std::round(std::abs(event.wheelDelta))));
            ScrollBy(direction * stepCount);
            UpdateHoveredItem(event.localPosition);
            return;
        }

        if (event.type == UiPointerEventType::Press && expanded_ &&
            PopupBounds().Contains(event.localPosition))
        {
            trackingDrag_ = true;
            dragMoved_ = false;
            dragStartY_ = event.localPosition.y;
            dragStartFirstVisibleIndex_ = firstVisibleIndex_;
            return;
        }

        if (event.type == UiPointerEventType::Release)
        {
            trackingDrag_ = false;
            return;
        }

        if (event.type != UiPointerEventType::Click)
        {
            return;
        }

        if (!expanded_)
        {
            expanded_ = true;
            EnsureSelectedItemVisible();
            UpdateHoveredItem(event.localPosition);
            return;
        }

        if (dragMoved_)
        {
            dragMoved_ = false;
            return;
        }

        if (const std::optional<std::size_t> selected =
                ItemIndexAt(event.localPosition);
            selected.has_value())
        {
            selectedIndex_ = *selected;
            RefreshText();
            Collapse();
            actions.push_back(UiAction{
                UiActionType::SelectionChanged,
                Id(),
                static_cast<float>(selectedIndex_),
                selectedIndex_,
                event.timestampTicks});
            return;
        }

        if (UiRect{0.0F, 0.0F, Bounds().width, Bounds().height}.Contains(
                event.localPosition))
        {
            Collapse();
        }
    }

    void UiComboBox::RefreshText()
    {
        SetText(items_.empty() ? std::wstring{} : items_[selectedIndex_]);
    }

    UiRect UiComboBox::PopupBounds() const noexcept
    {
        return {
            0.0F,
            Bounds().height,
            Bounds().width,
            itemHeight_ * static_cast<float>(VisibleItemCount())};
    }

    std::size_t UiComboBox::VisibleItemCount() const noexcept
    {
        return std::min(items_.size(), maxVisibleItems_);
    }

    std::optional<std::size_t> UiComboBox::ItemIndexAt(
        const UiPoint localPosition) const noexcept
    {
        const UiRect popup = PopupBounds();
        if (!popup.Contains(localPosition) || itemHeight_ <= 0.0F)
        {
            return std::nullopt;
        }

        const std::size_t row = static_cast<std::size_t>(
            (localPosition.y - popup.y) / itemHeight_);
        const std::size_t index = firstVisibleIndex_ + row;
        return row < VisibleItemCount() && index < items_.size()
            ? std::optional<std::size_t>{index}
            : std::nullopt;
    }

    void UiComboBox::EnsureSelectedItemVisible() noexcept
    {
        const std::size_t visibleCount = VisibleItemCount();
        if (visibleCount == 0)
        {
            firstVisibleIndex_ = 0;
            return;
        }

        if (selectedIndex_ < firstVisibleIndex_)
        {
            firstVisibleIndex_ = selectedIndex_;
        }
        else if (selectedIndex_ >= firstVisibleIndex_ + visibleCount)
        {
            firstVisibleIndex_ = selectedIndex_ - visibleCount + 1;
        }

        const std::size_t maxFirstIndex = items_.size() - visibleCount;
        firstVisibleIndex_ = std::min(firstVisibleIndex_, maxFirstIndex);
    }

    void UiComboBox::ScrollBy(const int itemDelta) noexcept
    {
        const std::size_t visibleCount = VisibleItemCount();
        if (visibleCount == 0 || items_.size() <= visibleCount)
        {
            return;
        }

        const int maxFirstIndex = static_cast<int>(items_.size() - visibleCount);
        const int requested = static_cast<int>(firstVisibleIndex_) + itemDelta;
        firstVisibleIndex_ = static_cast<std::size_t>(std::clamp(
            requested,
            0,
            maxFirstIndex));
    }

    void UiComboBox::UpdateHoveredItem(
        const UiPoint localPosition) noexcept
    {
        const std::optional<std::size_t> item = ItemIndexAt(localPosition);
        hoveredItemIndex_ = item.value_or(NoItemIndex);
    }

    bool UiComboBox::ContainsLocalPoint(
        const UiPoint localPosition) const noexcept
    {
        const UiRect mainBounds{0.0F, 0.0F, Bounds().width, Bounds().height};
        return mainBounds.Contains(localPosition) ||
            (expanded_ && PopupBounds().Contains(localPosition));
    }

}
