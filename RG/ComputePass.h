#pragma once
#include "Core/ComputePipeline.h"

//TODO: 生成合适的Pipeline IR 并传入Core层
namespace Ccino {

    struct ThreadGroupSize {
        uint32_t x;
        uint32_t y;
        uint32_t z;
    };

    class ComputePass {
    public:
        ComputePass();
        ~ComputePass();
    private:
        Ref<ComputePipeline> m_computePipeline; //这里是实际的管线对象
        ThreadGroupSize m_threadGroupSize;
    };
}