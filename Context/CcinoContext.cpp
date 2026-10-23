#include "Context/CcinoContext.h"
#include "ShaderStruct/ShaderIR.h"
#include "GLSLCompiler/GLSLCompiler.h"
#include "RHI/RHIStruct.h"
#include "CodeGenerator/CppParser.h"
#include "ShaderStruct/ShaderCommon.h"
#include <cstring>
#include <utility>
#include <iostream>

namespace Ccino {
    void Context::Run(const std::string& rgBuilderName) {
    }

    bool Context::Init(const RHISettings& settings) {
        hasInit = true;
        m_rhiContext.Init(settings);

        //反射当前的代码
        auto shaderIRs = CppParser::ParseFilePassShaders(CCINO_COMPILE_COMMAND_JSON_PATH, &globalFunctions);
        //处理ShaderIR生成SPV
        for(auto& shaderIR : shaderIRs) {
            //思考下如何处理VS 和 PS
            //现在只处理cs

            auto it = shaderIR.glsl.find("in;");
            if (it == std::string::npos) {
                std::cerr << "Error: the compute shader doesn't have thread group size !" << std::endl;
                hasInit = true;
                return false;
            } else {
                if (!globalFunctions.empty()) {
                    shaderIR.glsl.insert(it + 3, "\n" + globalFunctions);
                }
            }
            std::vector<uint32_t> spv = GLSLCompiler::CompileToSpv(shaderIR.passName ,shaderIR.glsl, ShaderType::Compute);
            shaderIR.glsl.clear();
            shaderIR.glsl.resize(sizeof(uint32_t) * spv.size());
            std::memcpy(shaderIR.glsl.data(), spv.data(), sizeof(uint32_t) * spv.size());
            shaderIR.size = sizeof(uint32_t) * spv.size();
        }
        ShaderIRRegistry::Instance().RegisterAll(std::move(shaderIRs));
        return true;

        //初始化完毕
    }
}
