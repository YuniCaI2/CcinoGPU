#pragma once
#include <concepts>
#include "RHI/RHIStruct.h"
#include "RG/ComputePass.h"

namespace Ccino {
    class RGBuilder {
    public:
        template<typename Func>
        requires std::is_invocable_v<Func, RGBuilder&>
        RGBuilder& AddPass(Func&& func) {
            //处理Accessor 并且处理Builder 内部Shader的翻译和绑定
        }

        //TODO: 定义外部RenderTarget 隐藏RHI RID
        template<typename VS, typename PS>
        void AddVSAndPS(uint32_t vertexCount, RID renderTargetRID, VS&& vs, PS&& ps) {
            //处理VS和PS的翻译和绑定
        }

        template<typename CS>
        void AddCS(const ThreadGroupSize& threadGroupSize, CS&& cs) {
            //这里的CS是一个Dummy，需要在编译时将其翻译成Pipeline 对象和ShaderModule
            //所以这里需要留一个槽位方便后续绑定使用
            //所以这里的CS需要是一个Pipeline 其中有Shader对象并且还有其周边的设置Meta数据
        }

    };
}