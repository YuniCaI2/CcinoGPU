#pragma once
#include <cstdint>
#include <variant>
#include "Core/RefCounted.hpp"
#include "RHI/RHIStruct.h"
#include "Core/Buffer.h"
#include "Core/Flags.h"

namespace Ccino {
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

    //上面两个对象是不会同时存在的
    using ResourceAccessFlags = std::variant<BufferAccessFlags, TextureAccessFlags>;


    //这个模板用来让Accessor模拟View
    template<typename BufferResourceType>
    class Accessor {
    public:
        Accessor(RID& resource, BufferAccessFlags bufferAccessFlags) {
            m_resource = resource;
            m_bufferAccessFlags = bufferAccessFlags;
        }
        //TODO: 支持Texture

        BufferResourceType operator[](uint32_t index) {
            //dummy 本质是占位符并且将类型传递出去让ClangAST看到，方便后续转换代码处理auto时可用 
        }

    private:
        RID m_resource; //这里是RGResource逻辑资源的RID
        BufferAccessFlags m_bufferAccessFlags;
    };
    //只作为Buffer的视图，后续再实现Texture确认边界
}