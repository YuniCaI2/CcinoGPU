#pragma once
#include<cstdint>
#include<vector>

namespace Ccino {
    enum class RGPassType {
        Compute,
        Graphics 
    };

    struct RGPass {
        //具体执行，这里和普通的RGPass不同的是这里直接消费IR从固定地址.Ccino/RG/{PassName}_IR.json
        RGPassType passType;
        std::vector<uint32_t> inputEdges;
        std::vector<uint32_t> outputEdges;
    };
}