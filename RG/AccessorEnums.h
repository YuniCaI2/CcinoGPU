#pragma once
#include <cstdint>
#include "Core/Flags.h"
#include <variant>

enum class BufferAccessFlagBits : uint32_t {
    Read = 1 << 0,
    Write = 1 << 1,
    ReadWrite = Read | Write,
    None = 0
};
using BufferAccessFlags = Flags<BufferAccessFlagBits>;

enum class TextureAccessFlagBits : uint32_t {
    Sampled  = 1 << 0,
    StorageRead = 1 << 1,
    StorageWrite = 1 << 2,
    StorageReadWrite = StorageRead | StorageWrite,
    RenderTarget = 1 << 3,
    None = 0
};
using TextureAccessFlags = Flags<TextureAccessFlagBits>;

enum class ASAccessFlagBits : uint32_t {
    Read = 1 << 0,
    None = 0
};
using ASAccessFlags = Flags<ASAccessFlagBits>;

// 放 AccessorEnums.h
constexpr bool HasTextureAccess(TextureAccessFlags flags, TextureAccessFlagBits bit) {
    return (flags.mask & static_cast<uint32_t>(bit)) != 0;
}

template<TextureAccessFlags Access, TextureAccessFlagBits Bit>
concept TextureAccessHas = HasTextureAccess(Access, Bit);

//资源访问标志互斥，同一条边只会是其中之一
using ResourceAccessFlags = std::variant<BufferAccessFlags, TextureAccessFlags, ASAccessFlags>;