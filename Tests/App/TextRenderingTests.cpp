#include "TextRenderingTests.h"
#include "MRG_Core.h"
#include <cstdlib>
#include <d3d12sdklayers.h>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace
{
    void Require(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    class TextClient final : public mrg::IGameClient
    {
      public:
        TextClient(bool &completed, Microsoft::WRL::ComPtr<ID3D12InfoQueue> &queue)
            : completed_(completed), queue_(queue)
        {
        }
        mrg::EngineConfig GetEngineConfig() const override
        {
            mrg::EngineConfig config;
            config.showWindow = false;
            config.renderRateOverrideHz = 120;
            config.audio.preferredBackend = mrg::audio::AudioOutputBackend::NoSound;
            return config;
        }
        void Initialize(const mrg::EngineServices &services) override
        {
            renderer_ = &services.textRendering;
            font_ = renderer_->LoadSystemFont(L"Segoe UI");
        }
        bool Update(const mrg::UpdateContext &) override
        {
            if (checkedFrames_ < frames_)
            {
                CheckFrame(checkedFrames_, renderer_->Statistics());
                checkedFrames_ = frames_;
            }
            if (checkedFrames_ == 8)
                completed_ = true;
            return checkedFrames_ < 8;
        }
        void Render(const mrg::graphics::RenderContext &context) override
        {
            if (frames_ == 0)
                static_cast<void>(context.meshRendering->Device()->QueryInterface(IID_PPV_ARGS(queue_.GetAddressOf())));
            mrg::graphics::TextDrawCommand draw;
            draw.style.font = font_;
            draw.style.fontSizePixels = 16;
            draw.layoutSizePixels = {300, 40};
            draw.positionPixels = {20, 20};
            draw.clipRectPixels = {0, 0, static_cast<float>(context.width), static_cast<float>(context.height)};
            switch (frames_)
            {
            case 0:
                for (int index = 0; index < 2049; ++index)
                    layouts_.push_back(renderer_->CreateLayout(L"Pinned layout " + std::to_wstring(index), draw));
                SubmitPinned(draw);
                break;
            case 1:
                SubmitPinned(draw);
                break;
            case 2:
                draw.clipRectPixels = {20, 20, 80, 60};
                renderer_->Submit(renderer_->CreateLayout(std::wstring(4096, L'W'), draw), draw);
                break;
            case 3:
                for (std::size_t row = 0; row < 32; ++row)
                {
                    std::wstring text;
                    for (std::size_t glyph = 0; glyph < 16; ++glyph)
                        text.push_back(static_cast<wchar_t>(0xAC00U + row * 16 + glyph));
                    draw.positionPixels = {static_cast<float>(row % 4) * 310, static_cast<float>(row / 4) * 40};
                    renderer_->Submit(renderer_->CreateLayout(text, draw), draw);
                }
                break;
            case 4:
            {
                std::wstring text;
                for (std::size_t index = 0; index < 6000; ++index)
                    text.push_back(static_cast<wchar_t>(0xAC00U + index));
                draw.style.fontSizePixels = 64;
                // Intentionally request every glyph for the residency stress test.
                draw.clipRectPixels = {0, 0, 1000000, 1000000};
                large_ = renderer_->CreateLayout(text, draw);
                renderer_->Submit(large_, draw);
                break;
            }
            case 5:
            case 6:
                renderer_->Submit(layouts_.front(), draw);
                break;
            case 7:
                draw.style.fontSizePixels = 64;
                renderer_->Submit(renderer_->CreateLayout(std::wstring(1, static_cast<wchar_t>(0xAC00U + 5999)), draw),
                                  draw);
                break;
            }
            ++frames_;
        }
        void OnResize(std::uint32_t, std::uint32_t) override
        {
        }
        void Shutdown() noexcept override
        {
        }

      private:
        void SubmitPinned(const mrg::graphics::TextDrawCommand &draw)
        {
            for (const auto &layout : layouts_)
                renderer_->Submit(layout, draw);
        }
        void CheckFrame(std::size_t frame, const mrg::graphics::TextRenderStatistics &stats)
        {
            if (frame >= 5 && stats.atlasEvictions > 0)
                reclaimed_ = true;
            if (frame == 0)
                Require(stats.layoutCreations == 2049, "Initial layouts must be prepared once.");
            if (frame == 1)
                Require(stats.layoutCreations == 0 && stats.glyphRasterizations == 0,
                        "Pinned layouts survive LRU eviction without reshaping or rasterization.");
            if (frame == 2)
                Require(stats.submittedGlyphs > 0 && stats.submittedGlyphs < 32,
                        "Clipped long titles must submit only nearby glyphs.");
            if (frame == 3)
                Require(stats.glyphRasterizations == 512 && stats.uploadAllocations == 1,
                        "512 CJK glyphs must share one upload allocation.");
            if (frame == 4)
                Require(stats.atlasPages > 16, "Current glyphs may exceed the soft atlas budget.");
            if (frame == 6)
                Require(stats.atlasPages <= 16 && reclaimed_,
                        "Unused overflow pages must be reclaimed after their fences.");
            if (frame == 7)
                Require(stats.glyphRasterizations == 1,
                        "An evicted glyph must be regenerated instead of using an old UV.");
            std::cout << "Text frame " << frame << ": layouts=" << stats.layoutCreations
                      << ", glyphs=" << stats.submittedGlyphs << ", raster=" << stats.glyphRasterizations
                      << ", uploads=" << stats.uploadAllocations << ", pages=" << stats.atlasPages
                      << ", evictions=" << stats.atlasEvictions << '\n';
        }
        bool &completed_;
        Microsoft::WRL::ComPtr<ID3D12InfoQueue> &queue_;
        mrg::graphics::TextRenderSystem *renderer_{};
        mrg::graphics::FontHandle font_;
        std::vector<mrg::graphics::TextLayoutHandle> layouts_;
        mrg::graphics::TextLayoutHandle large_;
        std::size_t frames_{}, checkedFrames_{};
        bool reclaimed_{};
    };
} // namespace

int TestTextRendering()
{
    bool completed = false;
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> queue;
    const int result = mrg::Run(std::make_unique<TextClient>(completed, queue));
    if (queue)
    {
        for (UINT64 index = 0; index < queue->GetNumStoredMessages(); ++index)
        {
            SIZE_T bytes = 0;
            if (FAILED(queue->GetMessage(index, nullptr, &bytes)))
                return EXIT_FAILURE;
            std::vector<std::byte> storage(bytes);
            auto *message = reinterpret_cast<D3D12_MESSAGE *>(storage.data());
            if (FAILED(queue->GetMessage(index, message, &bytes)))
                return EXIT_FAILURE;
            if (message->Severity == D3D12_MESSAGE_SEVERITY_ERROR ||
                message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION)
            {
                std::cerr << message->pDescription << '\n';
                return EXIT_FAILURE;
            }
        }
        std::cout << "D3D12 validation: no error/corruption messages.\n";
    }
    return result == EXIT_SUCCESS && completed ? EXIT_SUCCESS : EXIT_FAILURE;
}
