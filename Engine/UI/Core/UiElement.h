#pragma once

// Backend-neutral retained-mode UI primitives. Coordinates use a canvas-local
// top-left origin and increase to the right and downward.

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace mrg::ui
{
    using UiElementId = std::uint64_t;

    struct UiPoint
    {
        float x{};
        float y{};
    };

    struct UiSize
    {
        float width{};
        float height{};
    };

    struct UiRect
    {
        float x{};
        float y{};
        float width{};
        float height{};

        [[nodiscard]] bool Contains(UiPoint point) const noexcept;
    };

    struct UiColor
    {
        float red{};
        float green{};
        float blue{};
        float alpha{1.0F};
    };

    struct UiVisualStyle
    {
        UiColor normal{0.20F, 0.22F, 0.27F, 1.0F};
        UiColor hovered{0.28F, 0.32F, 0.40F, 1.0F};
        UiColor pressed{0.12F, 0.16F, 0.24F, 1.0F};
        UiColor disabled{0.14F, 0.14F, 0.16F, 0.65F};
    };

    enum class UiDrawCommandType : std::uint8_t
    {
        Rectangle,
        Text,
    };

    enum class UiTextAlignment : std::uint8_t
    {
        Leading,
        Center,
        Trailing,
    };

    struct UiDrawCommand
    {
        UiDrawCommandType type{UiDrawCommandType::Rectangle};
        UiRect bounds{};
        UiColor color{};
        std::wstring text;
        float fontSize{18.0F};
        UiTextAlignment horizontalAlignment{UiTextAlignment::Leading};
    };

    enum class UiPointerEventType : std::uint8_t
    {
        Enter,
        Leave,
        Move,
        Press,
        Release,
        Click,
    };

    enum class UiPointerButton : std::uint8_t
    {
        None,
        Left,
        Right,
        Middle,
    };

    struct UiPointerEvent
    {
        UiPointerEventType type{};
        UiPointerButton button{UiPointerButton::None};
        UiPoint canvasPosition{};
        UiPoint localPosition{};
        std::int64_t timestampTicks{};
    };

    enum class UiActionType : std::uint8_t
    {
        Clicked,
        ValueChanged,
        SelectionChanged,
    };

    struct UiAction
    {
        UiActionType type{};
        UiElementId source{};
        float value{};
        std::size_t selectedIndex{};
        std::int64_t timestampTicks{};
    };

    class UiCanvas;
    class UiInputRouter;

    class UiElement
    {
    public:
        UiElement();
        virtual ~UiElement();

        UiElement(const UiElement&) = delete;
        UiElement& operator=(const UiElement&) = delete;
        UiElement(UiElement&&) = delete;
        UiElement& operator=(UiElement&&) = delete;

        [[nodiscard]] UiElementId Id() const noexcept;
        [[nodiscard]] const UiRect& Bounds() const noexcept;
        void SetBounds(const UiRect& bounds);
        [[nodiscard]] UiRect BoundsInCanvas() const noexcept;

        [[nodiscard]] bool IsVisible() const noexcept;
        void SetVisible(bool visible) noexcept;
        [[nodiscard]] bool IsEnabled() const noexcept;
        void SetEnabled(bool enabled) noexcept;
        [[nodiscard]] bool IsHitTestVisible() const noexcept;
        void SetHitTestVisible(bool visible) noexcept;
        [[nodiscard]] bool IsHovered() const noexcept;
        [[nodiscard]] bool IsPressed() const noexcept;

        [[nodiscard]] const UiVisualStyle& Style() const noexcept;
        void SetStyle(const UiVisualStyle& style) noexcept;

        [[nodiscard]] UiElement* Parent() noexcept;
        [[nodiscard]] const UiElement* Parent() const noexcept;
        [[nodiscard]] const std::vector<std::unique_ptr<UiElement>>& Children()
            const noexcept;

        UiElement& AddChild(std::unique_ptr<UiElement> child);

        template <typename ElementType, typename... ArgumentTypes>
            requires std::is_base_of_v<UiElement, ElementType>
        ElementType& EmplaceChild(ArgumentTypes&&... arguments)
        {
            auto child = std::make_unique<ElementType>(
                std::forward<ArgumentTypes>(arguments)...);
            ElementType& result = *child;
            AddChild(std::move(child));
            return result;
        }

        [[nodiscard]] bool RemoveChild(UiElementId id) noexcept;

    protected:
        [[nodiscard]] UiColor CurrentBackgroundColor() const noexcept;
        virtual void AppendDrawCommands(
            std::vector<UiDrawCommand>& commands,
            const UiRect& absoluteBounds) const;
        virtual void OnPointerEvent(
            const UiPointerEvent& event,
            std::vector<UiAction>& actions);

    private:
        friend class UiCanvas;
        friend class UiInputRouter;

        struct HitResult
        {
            UiElement* element{};
            UiPoint localPosition{};
        };

        [[nodiscard]] HitResult HitTest(UiPoint parentPosition) noexcept;
        [[nodiscard]] UiElement* Find(UiElementId id) noexcept;
        [[nodiscard]] const UiElement* Find(UiElementId id) const noexcept;
        void CollectDrawCommands(
            std::vector<UiDrawCommand>& commands,
            UiPoint parentOrigin) const;
        void SetHovered(bool hovered) noexcept;
        void SetPressed(bool pressed) noexcept;

        UiElementId id_{};
        UiRect bounds_{};
        UiVisualStyle style_{};
        UiElement* parent_{};
        std::vector<std::unique_ptr<UiElement>> children_;
        bool visible_{true};
        bool enabled_{true};
        bool hitTestVisible_{true};
        bool hovered_{};
        bool pressed_{};
    };
}
