#pragma once
#include "Flags.h"
#include<cstdint>
#include<vector>

namespace Ccino {
    enum class RGPassTypeFlagBits : uint32_t {
        Compute = 1 << 0,
        Graphics = 1 << 1 
    };

    using RGPassTypeFlags = Flags<RGPassTypeFlagBits>;

    struct RGPass {
        //具体执行，这里和普通的RGPass不同的是这里直接消费IR从固定地址.Ccino/RG/{PassName}_IR.json
        RGPassTypeFlagBits passType;
    };
}