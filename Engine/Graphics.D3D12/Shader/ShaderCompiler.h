#pragma once

#include <d3dcommon.h>
#include <wrl/client.h>

#include <cstdint>

namespace mrg::graphics::detail
{
    enum class BuiltInShader : std::uint8_t
    {
        UnlitVertexColorInstanced,
        UnlitVertexColorTextureArrayInstanced,
        Text,
        UiRectangle,
    };

    [[nodiscard]] Microsoft::WRL::ComPtr<ID3DBlob> CompileShader(
        BuiltInShader shader,
        const char* entryPoint,
        const char* target);
}
