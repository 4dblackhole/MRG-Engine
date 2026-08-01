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

    void UiComboBox::SetItems(std::vector<std::wstring> items)
    {
        items_ = std::move(items);
        if (selectedIndex_ >= items_.size())
        {
            selectedIndex_ = 0;
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
        RefreshText();
    }

    void UiComboBox::OnPointerEvent(
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

    void UiComboBox::RefreshText()
    {
        SetText(items_.empty() ? std::wstring{} : items_[selectedIndex_]);
    }
}
