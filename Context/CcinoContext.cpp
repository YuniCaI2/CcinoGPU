#include "Context/CcinoContext.h"
#include "RenderGraph.h"
#include "ShaderStruct/ShaderIR.h"
#include "GLSLCompiler/GLSLCompiler.h"
#include "RHI/RHIStruct.h"
#include "ShaderStruct/ShaderCommon.h"
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <istream>
#include <memory>
#include <utility>
#include <iostream>

namespace Ccino {
    bool Context::Run(const std::string& rgBuilderName) {
        if (m_rgBuilders.contains(rgBuilderName) && m_rgBuilders[rgBuilderName] != nullptr) {
            m_rgBuilders[rgBuilderName]->Run();
            return true;
        }
        return false;
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
    void Context::Destroy() {
        for (auto& [name, builder] : m_rgBuilders) {
            if (builder != nullptr) {
                builder->Destroy();
            }
        }
        m_rgBuilders.clear();
        m_rhiContext.Destroy();
    }

    RGBuilder* Context::CreateRGBuilder(const std::string& name) {
        //创建RGBuilder
        if (m_rgBuilders.contains(name)) {
            //覆盖资源
            m_rgBuilders[name]->Destroy();
        }

        auto builder = std::make_unique<RGBuilder>();
        builder->Init(&m_rhiContext);
        m_rgBuilders[name] = std::move(builder);

        return m_rgBuilders[name].get();
    }

    bool Context::DestroyRGBuilder(const std::string& name) {
        if (m_rgBuilders.contains(name)) {
            if (m_rgBuilders[name] == nullptr) return true;
            m_rgBuilders[name]->Destroy();
            m_rgBuilders[name] = nullptr;
            return true;
        } 
        return false;
    }

    RGBuilder* Context::GetRGBuilder(const std::string& name) {
        if (m_rgBuilders.contains(name)) {
            return m_rgBuilders[name].get();
        } else {
            return nullptr;
        }
    }
}
