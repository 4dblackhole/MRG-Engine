#pragma once

#include "Core/UiElement.h"

#include <optional>
#include <string_view>

namespace mrg::ui
{
    class UiPanel : public UiElement
    {
    public:
        UiPanel() = default;
        ~UiPanel() override = default;
    };

    class UiLabel final : public UiElement
    {
    public:
        explicit UiLabel(std::wstring text = {});

        [[nodiscard]] std::wstring_view Text() const noexcept;
        void SetText(std::wstring text);
        [[nodiscard]] float FontSize() const noexcept;
        void SetFontSize(float fontSize);
        [[nodiscard]] UiColor TextColor() const noexcept;
        void SetTextColor(UiColor color) noexcept;
        void SetHorizontalAlignment(UiTextAlignment alignment) noexcept;

    protected:
        void AppendDrawCommands(
            std::vector<UiDrawCommand>& commands,
            const UiRect& absoluteBounds) const override;

    private:
        std::wstring text_;
        float fontSize_{18.0F};
        UiColor textColor_{1.0F, 1.0F, 1.0F, 1.0F};
        UiTextAlignment alignment_{UiTextAlignment::Leading};
    };

    // Displays one renderer-owned raster image. UiImageHandle is opaque so
    // the retained UI tree stays independent from D3D12 texture resources.
    class UiImage final : public UiElement
    {
    public:
        UiImage() = default;

        [[nodiscard]] UiImageHandle Image() const noexcept;
        void SetImage(UiImageHandle image) noexcept;
        [[nodiscard]] UiColor Tint() const noexcept;
        void SetTint(UiColor tint) noexcept;

    protected:
        void AppendDrawCommands(
            std::vector<UiDrawCommand>& commands,
            const UiRect& absoluteBounds) const override;

    private:
        UiImageHandle image_{};
        UiColor tint_{1.0F, 1.0F, 1.0F, 1.0F};
    };

    class UiButton : public UiElement
    {
    public:
        explicit UiButton(std::wstring text = {});

        [[nodiscard]] std::wstring_view Text() const noexcept;
        void SetText(std::wstring text);
        void SetFontSize(float fontSize);

    protected:
        void AppendDrawCommands(
            std::vector<UiDrawCommand>& commands,
            const UiRect& absoluteBounds) const override;
        void OnPointerEvent(
            const UiPointerEvent& event,
            std::vector<UiAction>& actions) override;

        [[nodiscard]] float FontSize() const noexcept;

    private:
        std::wstring text_;
        float fontSize_{17.0F};
    };

    class UiToggle final : public UiButton
    {
    public:
        explicit UiToggle(std::wstring text = {}, bool checked = false);

        [[nodiscard]] bool IsChecked() const noexcept;
        void SetChecked(bool checked) noexcept;

    protected:
        void AppendDrawCommands(
            std::vector<UiDrawCommand>& commands,
            const UiRect& absoluteBounds) const override;
        void OnPointerEvent(
            const UiPointerEvent& event,
            std::vector<UiAction>& actions) override;

    private:
        bool checked_{};
    };

    class UiSlider final : public UiElement
    {
    public:
        explicit UiSlider(float value = 0.0F);

        [[nodiscard]] float Value() const noexcept;
        void SetValue(float value) noexcept;

    protected:
        void AppendDrawCommands(
            std::vector<UiDrawCommand>& commands,
            const UiRect& absoluteBounds) const override;
        void OnPointerEvent(
            const UiPointerEvent& event,
            std::vector<UiAction>& actions) override;

    private:
        void UpdateFromPointer(
            const UiPointerEvent& event,
            std::vector<UiAction>& actions);

        float value_{};
    };

    // Preserves the original click-to-advance selection behavior for compact
    // settings such as AUTO/WASAPI/ASIO where a popup would add no value.
    class UiCycleSelector final : public UiButton
    {
    public:
        UiCycleSelector() = default;

        void SetItems(std::vector<std::wstring> items);
        [[nodiscard]] const std::vector<std::wstring>& Items() const noexcept;
        [[nodiscard]] std::size_t SelectedIndex() const noexcept;
        void SetSelectedIndex(std::size_t index);

    protected:
        void OnPointerEvent(
            const UiPointerEvent& event,
            std::vector<UiAction>& actions) override;

    private:
        void RefreshText();

        std::vector<std::wstring> items_;
        std::size_t selectedIndex_{};
    };

    class UiComboBox final : public UiButton
    {
    public:
        UiComboBox() = default;

        void SetItems(std::vector<std::wstring> items);
        [[nodiscard]] const std::vector<std::wstring>& Items() const noexcept;
        [[nodiscard]] std::size_t SelectedIndex() const noexcept;
        void SetSelectedIndex(std::size_t index);
        void SetMaxVisibleItems(std::size_t maxVisibleItems);
        [[nodiscard]] std::size_t MaxVisibleItems() const noexcept;
        void SetItemHeight(float itemHeight);
        [[nodiscard]] float ItemHeight() const noexcept;
        [[nodiscard]] bool IsExpanded() const noexcept;
        void Collapse() noexcept;

    protected:
        void AppendDrawCommands(
            std::vector<UiDrawCommand>& commands,
            const UiRect& absoluteBounds) const override;
        void OnPointerEvent(
            const UiPointerEvent& event,
            std::vector<UiAction>& actions) override;
        [[nodiscard]] bool ContainsLocalPoint(
            UiPoint localPosition) const noexcept override;

    private:
        void RefreshText();
        [[nodiscard]] UiRect PopupBounds() const noexcept;
        [[nodiscard]] std::size_t VisibleItemCount() const noexcept;
        [[nodiscard]] std::optional<std::size_t> ItemIndexAt(
            UiPoint localPosition) const noexcept;
        void EnsureSelectedItemVisible() noexcept;
        void ScrollBy(int itemDelta) noexcept;
        void UpdateHoveredItem(UiPoint localPosition) noexcept;

        std::vector<std::wstring> items_;
        std::size_t selectedIndex_{};
        std::size_t firstVisibleIndex_{};
        std::size_t maxVisibleItems_{4};
        std::size_t hoveredItemIndex_{static_cast<std::size_t>(-1)};
        float itemHeight_{36.0F};
        float dragStartY_{};
        std::size_t dragStartFirstVisibleIndex_{};
        bool expanded_{};
        bool trackingDrag_{};
        bool dragMoved_{};
    };
}
