#pragma once
#include "RHI/Context.h"
#include <memory>
#include <unordered_map>
#include "RG/RenderGraph.h"

namespace Ccino {
    class Context {
    public:
        bool Init(const RHISettings& settings); //必须要初始化
        bool Run(const std::string& rgBuilderName);
        void Destroy();


        //创建RGBuilder
        RGBuilder* CreateRGBuilder(const std::string& name);
        bool DestroyRGBuilder(const std::string& name);
        RGBuilder* GetRGBuilder(const std::string& name);

        Ref<Buffer> CreateBuffer(const Buffer::CreateArgs& createArgs) {
            return Buffer::Create(m_rhiContext, createArgs);
        }

        RHI::Context& GetRHIContext() { return m_rhiContext; }



    private:
        std::unordered_map<std::string , std::unique_ptr<RGBuilder>> m_rgBuilders;
        RHI::Context m_rhiContext;
        bool hasInit = false;
        std::vector<ShaderIR> m_shaderIRs;
        std::string globalFunctions; //其中包含了全局函数和结构体
    };
};