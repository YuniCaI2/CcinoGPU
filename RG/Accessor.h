#pragma once
#include <cstdint>
#include <variant>
#include "Core/RefCounted.hpp"
#include "RHI/RHIStruct.h"
#include "Core/Buffer.h"

namespace Ccino {
    enum class BufferAccessFlagBits : uint32_t {
        Read = 1 << 0,
        Write = 1 << 1,
        ReadWrite = Read | Write,
        None = 0
    };
    using BufferAccessFlags = uint32_t;

    enum class TextureAccessFlagBits : uint32_t {
        Sampled  = 1 << 0,
        StorageRead = 1 << 1,
        StorageWrite = 1 << 2,
        StorageReadWrite = StorageRead | StorageWrite,
        RenderTarget = 1 << 3,
        None = 0
    };
    using TextureAccessFlags = uint32_t;

    //上面两个对象是不会同时存在的
    using ResourceAccessFlags = std::variant<BufferAccessFlags, TextureAccessFlags>;



    class Accessor {
    public:
        //Buffer Accessor实现
        template<typename DataType, BufferType bufferType>
        Accessor(Ref<Buffer<DataType, bufferType>> buffer, BufferAccessFlags bufferAccessFlags) {
            m_rhi = buffer->GetRHI();
            m_bufferAccessFlags = bufferAccessFlags;
        }
        //TODO: 支持Texture
    private:
        RID m_rhi; //可能是Buffer或者Texture
        BufferAccessFlags m_bufferAccessFlags = 0;
        TextureAccessFlags m_textureAccessFlags = 0;
    };
}