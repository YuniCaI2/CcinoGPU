#pragma once
#include "Core/ComputePipeline.h"
#include "ShaderStruct/ShaderCommon.h"
#include <cstdint>

//TODO: 生成合适的Pipeline IR 并传入Core层
namespace Ccino {
    class ComputePass {
    public:
        ComputePass();
        ~ComputePass();
    private:
        Ref<ComputePipeline> m_computePipeline; //这里是实际的管线对象
        ThreadGroupSize m_threadGroupSize;
    };
}