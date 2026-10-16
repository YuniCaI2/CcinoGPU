#pragma once
#include "Core/RefCounted.hpp"
#include "RHI/Context.h"

namespace Ccino {
    class ComputePipeline : public RefCounted {
    public:
        virtual ~ComputePipeline() {

        }

        Ref<ComputePipeline> Create(RHI::Context& context, const std::string& name) {
            auto computePipeline = Ref<ComputePipeline>::Make();
            computePipeline->CreateImpl(context, name /*,Pipeline IR*/); //管线层有Code Cpp代码？ 将代码注入到当前的依赖里？ 或者将IR写成JSON然后消费
            return computePipeline;
        }

        void Delete() {

        }

    private:
        void CreateImpl(RHI::Context& context, const std::string& name) {
            //TODO
        }
    

    };
}