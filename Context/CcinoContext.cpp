#include "Context/CcinoContext.h"
#include "ShaderStruct/ShaderIR.h"
#include "GLSLCompiler/GLSLCompiler.h"
#include "RHI/RHIStruct.h"
#include "ShaderStruct/ShaderCommon.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <istream>
#include <utility>
#include <iostream>

namespace Ccino {
    void Context::Run(const std::string& rgBuilderName) {
    }

    bool Context::Init(const RHISettings& settings) {
        hasInit = true;
        m_rhiContext.Init(settings);

        //反射当前的代码
        std::filesystem::path shaderDir(CCINO_SHADER_DIR);
        std::filesystem::path globalFunctionsPath(CCINO_GLOBAL_HEADER_FILE);

        //获得全局函数
        std::ifstream globalHeaderFile(globalFunctionsPath, std::ios::binary);
        globalFunctions = std::string(std::istreambuf_iterator<char>(globalHeaderFile),
                       std::istreambuf_iterator<char>());

        std::vector<ShaderIR> shaderIRs;
        for (const auto& entry : std::filesystem::directory_iterator(shaderDir)) {
            const std::filesystem::path& shaderPath = entry.path();
            if (!entry.is_regular_file() || shaderPath.extension() != ".json") {
                continue;
            }
            std::ifstream shaderFile(shaderPath);
            nlohmann::json shaderJson = nlohmann::json::parse(shaderFile);
            if (!shaderJson.is_object() || !shaderJson.contains("passName")) {
                continue;
            }
            auto shaderIR = shaderJson.get<ShaderIR>();
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

            shaderIRs.push_back(std::move(shaderIR));
        }

        //处理ShaderIR生成SPV
        ShaderIRRegistry::Instance().RegisterAll(std::move(shaderIRs));
        return true;

        //初始化完毕
    }
}
