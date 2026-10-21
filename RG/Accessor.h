#pragma once
#include <cstdint>
#include <variant>
#include "Core/RefCounted.hpp"
#include "RHI/RHIStruct.h"
#include "Core/Buffer.h"
#include "Core/Flags.h"
#include "RG/AccessorEnums.h"

namespace Ccino {


    //这个模板用来让Accessor模拟View
    template<typename BufferResourceType, BufferAccessFlags bufferAccessFlags>
    class Accessor {
    public:
        Accessor(RID& resource) {
            m_resource = resource;
        }
        //TODO: 支持Texture

        BufferResourceType& operator[](uint32_t index) const {
            //dummy 本质是占位符并且将类型传递出去让ClangAST看到，方便后续转换代码处理auto时可用 
            static BufferResourceType dummy{};
            return dummy;
        }

    private:
        RID m_resource; //这里是RGResource逻辑资源的RID
        BufferAccessFlags m_bufferAccessFlags = bufferAccessFlags;
    };
    //只作为Buffer的视图，后续再实现Texture确认边界
}