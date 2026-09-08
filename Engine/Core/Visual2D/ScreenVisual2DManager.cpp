#include "Visual2D/ScreenVisual2DManager.h"

#include "Visual2D/Visual2DRendering.h"

#include <stdexcept>

namespace mrg::visual2d
{
    ScreenVisual2DManager::~ScreenVisual2DManager()
    {
        Shutdown();
    }

    void ScreenVisual2DManager::Initialize(
        graphics::Visual2DRenderSystem& rendering,
        const Size viewportSize)
    {
        if (rendering_ != nullptr)
        {
            throw std::logic_error(
                "ScreenVisual2DManager is already initialized.");
        }
        rendering_ = &rendering;
        viewportSize_ = viewportSize;
    }

    void ScreenVisual2DManager::Shutdown() noexcept
    {
        canvases_.clear();
        images_.clear();
        rendering_ = nullptr;
        viewportSize_ = {};
    }

    ScreenCanvasId ScreenVisual2DManager::CreateCanvas(
        const ScreenCanvasSettings& settings)
    {
        if (rendering_ == nullptr)
        {
            throw std::logic_error(
                "ScreenVisual2DManager must be initialized before use.");
        }
        if (nextCanvasId_ == InvalidScreenCanvasId)
        {
            throw std::overflow_error("Screen Canvas IDs are exhausted.");
        }

        const ScreenCanvasId id = nextCanvasId_++;
        auto canvas = std::make_unique<Visual2DCanvas>(
            settings.referenceSize,
            settings.scaleMode);
        canvas->SetViewportSize(viewportSize_);
        canvases_.emplace(
            id,
            ScreenCanvas{
                std::move(canvas),
                settings.screenOrigin,
                settings.zOrder,
                settings.visible});
        return id;
    }

    Visual2DCanvas* ScreenVisual2DManager::FindCanvas(
        const ScreenCanvasId id) noexcept
    {
        const auto canvas = canvases_.find(id);
        return canvas == canvases_.end() ? nullptr : canvas->second.canvas.get();
    }

    const Visual2DCanvas* ScreenVisual2DManager::FindCanvas(
        const ScreenCanvasId id) const noexcept
    {
        return const_cast<ScreenVisual2DManager*>(this)->FindCanvas(id);
    }

    bool ScreenVisual2DManager::RemoveCanvas(const ScreenCanvasId id) noexcept
    {
        return canvases_.erase(id) != 0;
    }

    bool ScreenVisual2DManager::SetCanvasVisible(
        const ScreenCanvasId id,
        const bool visible) noexcept
    {
        const auto canvas = canvases_.find(id);
        if (canvas == canvases_.end())
        {
            return false;
        }
        canvas->second.visible = visible;
        return true;
    }

    bool ScreenVisual2DManager::SetCanvasPlacement(
        const ScreenCanvasId id,
        const Point screenOrigin,
        const std::uint32_t zOrder) noexcept
    {
        const auto canvas = canvases_.find(id);
        if (canvas == canvases_.end())
        {
            return false;
        }
        canvas->second.screenOrigin = screenOrigin;
        canvas->second.zOrder = zOrder;
        return true;
    }

    ImageHandle ScreenVisual2DManager::RegisterImage(
        const std::filesystem::path& path)
    {
        if (rendering_ == nullptr)
        {
            throw std::logic_error(
                "ScreenVisual2DManager must be initialized before use.");
        }
        const std::filesystem::path key = path.lexically_normal();
        const auto cached = images_.find(key);
        if (cached != images_.end())
        {
            return cached->second;
        }
        const ImageHandle image = rendering_->LoadImage(key);
        images_.emplace(key, image);
        return image;
    }

    Size ScreenVisual2DManager::GetImageSize(
        const ImageHandle image) const noexcept
    {
        return rendering_ == nullptr ? Size{} : rendering_->GetImageSize(image);
    }

    void ScreenVisual2DManager::Update(const double elapsedSeconds)
    {
        for (auto& [id, entry] : canvases_)
        {
            static_cast<void>(id);
            if (entry.visible)
            {
                entry.canvas->Update(elapsedSeconds);
            }
        }
    }

    void ScreenVisual2DManager::Render(
        const graphics::RenderContext& context)
    {
        if (rendering_ == nullptr)
        {
            return;
        }
        for (const auto& [id, entry] : canvases_)
        {
            static_cast<void>(id);
            if (entry.visible)
            {
                rendering_->SubmitScreen(
                    *entry.canvas,
                    context,
                    entry.screenOrigin,
                    entry.zOrder);
            }
        }
    }

    void ScreenVisual2DManager::OnResize(
        const std::uint32_t width,
        const std::uint32_t height)
    {
        viewportSize_ = {
            static_cast<float>(width),
            static_cast<float>(height)};
        for (auto& [id, entry] : canvases_)
        {
            static_cast<void>(id);
            entry.canvas->SetViewportSize(viewportSize_);
        }
    }

    std::size_t ScreenVisual2DManager::CanvasCount() const noexcept
    {
        return canvases_.size();
    }

    std::size_t ScreenVisual2DManager::ImageCount() const noexcept
    {
        return images_.size();
    }
}
