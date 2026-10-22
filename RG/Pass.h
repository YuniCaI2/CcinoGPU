#pragma once
#include "Flags.h"
#include "RHI/RHIStruct.h"
#include "ShaderStruct/ShaderCommon.h"
#include<cstdint>

namespace Ccino {
    enum class RGPassTypeFlagBits : uint32_t {
        Compute = 1 << 0,
        Graphics = 1 << 1 
    };

    using RGPassTypeFlags = Flags<RGPassTypeFlagBits>;

    struct RGPass {
        //具体执行，这里和普通的RGPass不同的是这里直接消费IR从固定地址.Ccino/RG/{PassName}_IR.json
        std::string passName;
        RGPassTypeFlagBits passType;
        RID m_pipleineRID = RID::Invalid();
        RID m_descriptorSlotRID = RID::Invalid();
        // void* lambdaPtr = nullptr; //不使用这个是因为临时的lambda可能会出现悬空
        std::vector<char> lambda; //这里直接实现拷贝

        //依赖的GPU资源表
        std::unordered_map<std::string, RID> resourceMap; //这里记录了Accessor处理的对象
        //Compute Info
        WorkGroupSize workGroupSize;
    };
}