#pragma once
#include "RHI/Context.h"
#include <unordered_map>
#include "RG/RenderGraph.h"

namespace Ccino {
    class Context {
    public:
        bool Init(const RHISettings& settings); //必须要初始化
        
        void Run(const std::string& rgBuilderName);

        //创建RGBuilder
        RGBuilder* CreateRGBuilder(const std::string& name);


    private:
        std::unordered_map<std::string , RGBuilder> m_rgBuilders;
        RHI::Context m_rhiContext;
        bool hasInit = false;
        std::vector<ShaderIR> m_shaderIRs;
        std::string globalFunctions; //其中包含了全局函数和结构体
    };
};