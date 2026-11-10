#include <iostream>
#include "CcinoType.h"
#include "RG/RenderGraph.h"
#include "ShaderStruct/ShaderCommon.h"
namespace  {
    struct TestPassesStruct {
        uint32_t val1{4};
    };
}

static void Test1() {
    Ccino::RGBuilder builder;
    builder.AddPass<"TestPassesPass">([&](Ccino::RGBuilder& builder, RID pass){
        Ccino::Vec3 s = {};
        builder.AddCS<Ccino::ThreadGroupSize{1,1,1}>([=](){
            Ccino::Vec3 v = -s;
            auto vs = s;
        }, Ccino::WorkGroupSize{1, 1, 1});
    });
}
