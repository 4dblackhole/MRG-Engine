#include "TextPixelTests.h"
#include "PhantomCore.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
    using Microsoft::WRL::ComPtr;
    constexpr UINT Width = 300, Height = 120;
    void Require(HRESULT result)
    {
        if (FAILED(result))
            throw std::runtime_error("Text pixel fixture graphics call failed.");
    }
    struct PixelFixture
    {
        TextRenderSystem text;
        FontHandle font;
        ComPtr<ID3D12Resource> target, readback;
        ComPtr<ID3D12DescriptorHeap> rtv;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};

        void Initialize(const RenderContext &context)
        {
            auto *device = context.meshRendering->Device();
            text.Initialize(*device, DXGI_FORMAT_R8G8B8A8_UNORM);
            font = text.LoadSystemFont(L"Segoe UI");
            CreateTarget(*device);
            CreateReadback(*device);
        }
        void CreateTarget(ID3D12Device &device)
        {
            D3D12_RESOURCE_DESC description{};
            description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            description.Width = Width;
            description.Height = Height;
            description.DepthOrArraySize = 1;
            description.MipLevels = 1;
            description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            description.SampleDesc.Count = 1;
            description.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            D3D12_HEAP_PROPERTIES properties{};
            properties.Type = D3D12_HEAP_TYPE_DEFAULT;
            Require(device.CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &description,
                                                   D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                                   IID_PPV_ARGS(target.GetAddressOf())));
            D3D12_DESCRIPTOR_HEAP_DESC heap{};
            heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            heap.NumDescriptors = 1;
            Require(device.CreateDescriptorHeap(&heap, IID_PPV_ARGS(rtv.GetAddressOf())));
            device.CreateRenderTargetView(target.Get(), nullptr, rtv->GetCPUDescriptorHandleForHeapStart());
        }
        void CreateReadback(ID3D12Device &device)
        {
            const auto description = target->GetDesc();
            UINT64 bytes = 0;
            device.GetCopyableFootprints(&description, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
            D3D12_RESOURCE_DESC buffer{};
            buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            buffer.Width = bytes;
            buffer.Height = 1;
            buffer.DepthOrArraySize = 1;
            buffer.MipLevels = 1;
            buffer.SampleDesc.Count = 1;
            buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            D3D12_HEAP_PROPERTIES properties{};
            properties.Type = D3D12_HEAP_TYPE_READBACK;
            Require(device.CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &buffer,
                                                   D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                   IID_PPV_ARGS(readback.GetAddressOf())));
        }
        void RecordReadback(ID3D12GraphicsCommandList &commands)
        {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition = {target.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                  D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE};
            commands.ResourceBarrier(1, &barrier);
            D3D12_TEXTURE_COPY_LOCATION destination{}, source{};
            destination.pResource = readback.Get();
            destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            destination.PlacedFootprint = footprint;
            source.pResource = target.Get();
            source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            commands.CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        }
        bool VerifyPixels()
        {
            void *mapped = nullptr;
            Require(readback->Map(0, nullptr, &mapped));
            const auto channel = [&](UINT x, UINT y, UINT color)
            {
                return static_cast<const unsigned char *>(
                           mapped)[footprint.Offset + static_cast<std::size_t>(y) * footprint.Footprint.RowPitch +
                                   x * 4 + color] /
                       255.0;
            };
            constexpr double background = 15.0 / 255;
            double error = 0;
            std::size_t covered = 0;
            for (UINT y = 0; y < 80; ++y)
                for (UINT x = 0; x < 80; ++x)
                {
                    const double a =
                        std::clamp((channel(110 + x, 10 + y, 0) - background) / (1 - background), 0.0, 1.0);
                    const double b =
                        std::clamp((channel(210 + x, 10 + y, 0) - background) / (1 - background), 0.0, 1.0);
                    if (a > .5 && b > .5)
                        ++covered;
                    const double behind = background * (1 - a) * (1 - b);
                    const std::array<double, 3> expected{b * (1 - a) + behind, behind, a + behind};
                    for (UINT color = 0; color < 3; ++color)
                        error += std::abs(channel(10 + x, 10 + y, color) - expected[color]) * 255;
                }
            const D3D12_RANGE noWrites{0, 0};
            readback->Unmap(0, &noWrites);
            error /= 80 * 80 * 3;
            std::cout << "Text alpha paint order: byte error=" << error << ", overlapping pixels=" << covered << '\n';
            return error < 1 && covered > 100;
        }
    };
    class PixelClient final : public IGameClient
    {
      public:
        explicit PixelClient(PixelFixture &fixture) : fixture_(fixture)
        {
        }
        EngineConfig GetEngineConfig() const override
        {
            EngineConfig config;
            config.showWindow = false;
            config.audio.preferredBackend = AudioOutputBackend::NoSound;
            return config;
        }
        void Initialize(const EngineServices &) override
        {
        }
        bool Update(const UpdateContext &) override
        {
            return !rendered_;
        }
        void Render(const RenderContext &context) override
        {
            fixture_.Initialize(context);
            auto &commands = *context.commandList;
            const auto target = fixture_.rtv->GetCPUDescriptorHandleForHeapStart();
            const float background[]{.06F, .06F, .06F, 1};
            commands.ClearRenderTargetView(target, background, 0, nullptr);
            const D3D12_VIEWPORT viewport{0, 0, Width, Height, 0, 1};
            const D3D12_RECT scissor{0, 0, Width, Height};
            commands.RSSetViewports(1, &viewport);
            commands.RSSetScissorRects(1, &scissor);
            commands.OMSetRenderTargets(1, &target, FALSE, nullptr);
            fixture_.text.BeginFrame(context.frameIndex, context.renderIndex);
            TextDrawCommand draw;
            draw.style.font = fixture_.font;
            draw.style.fontSizePixels = 64;
            draw.layoutSizePixels = {80, 80};
            draw.positionPixels = {0, 1000};
            fixture_.text.Submit(L"A", draw); // A occupies an early atlas page.
            std::wstring bank;
            for (UINT index = 0; index < 1000; ++index)
                bank.push_back(static_cast<wchar_t>(0xAC00U + index));
            fixture_.text.Submit(bank, draw);
            draw.positionPixels = {10, 10};
            draw.style.color = {1, 0, 0, 1};
            fixture_.text.Submit(L"B", draw); // B occupies a later page, but paints first.
            draw.style.color = {0, 0, 1, 1};
            fixture_.text.Submit(L"A", draw);
            draw.style.color = {1, 1, 1, 1};
            draw.positionPixels = {110, 10};
            fixture_.text.Submit(L"A", draw);
            draw.positionPixels = {210, 10};
            fixture_.text.Submit(L"B", draw);
            fixture_.text.Flush(commands, Width, Height);
            fixture_.RecordReadback(commands);
            commands.RSSetViewports(1, &context.viewport);
            commands.RSSetScissorRects(1, &context.scissorRectangle);
            commands.OMSetRenderTargets(1, &context.renderTargetView, FALSE, &context.depthStencilView);
            rendered_ = true;
        }
        void OnResize(std::uint32_t, std::uint32_t) override
        {
        }
        void Shutdown() noexcept override
        {
        }

      private:
        PixelFixture &fixture_;
        bool rendered_{};
    };
} // namespace

int TestTextPixelOrder()
{
    // Resources survive Run until the engine has completed all GPU work.
    PixelFixture fixture;
    if (Run(std::make_unique<PixelClient>(fixture)) != 0)
        return 1;
    return fixture.VerifyPixels() ? 0 : 1;
}
