#pragma once
#include "Core/Buffer.h"
#include "Core/ResourceConcept.h"

namespace Ccino {
    struct RGResource {
        ResourceType resourceType;
        using BufferCreateArgs = typename Buffer::CreateArgs;
        BufferCreateArgs bufferCreateArgs; //相当于描述
        bool isExternal = false; //是否是外部资源 如果是外部资源这里的仅仅为占位
        Ref<Buffer> bufferRef;
    };

}