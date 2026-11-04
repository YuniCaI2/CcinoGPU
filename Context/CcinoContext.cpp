#include "Context/CcinoContext.h"
#include "RenderGraph.h"
#include "ShaderStruct/ShaderIR.h"
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

        //加载编译期生成的 ShaderIR JSON 与预编译 SPV
        std::filesystem::path shaderDir(CCINO_SHADER_DIR);

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

            const std::filesystem::path spvFilePath = shaderIR.csSpvPath.empty()
                ? (shaderDir / (shaderIR.passName + ".spv"))
                : std::filesystem::path(shaderIR.csSpvPath);
            std::ifstream spvFile(spvFilePath, std::ios::binary | std::ios::ate);
            if (!spvFile.is_open()) {
                std::cerr << "Error: failed to open precompiled SPV file: " << spvFilePath << std::endl;
                return false;
            }
            const std::streamsize spvByteSize = spvFile.tellg();
            if (spvByteSize <= 0 || (spvByteSize % sizeof(uint32_t)) != 0) {
                std::cerr << "Error: invalid precompiled SPV file size: " << spvFilePath << std::endl;
                return false;
            }
            spvFile.seekg(0, std::ios::beg);
            std::string* shaderCode = &shaderIR.cs;
            uint32_t* shaderSize = &shaderIR.csSize;
            if (shaderIR.shaderType == ShaderType::Vertex) {
                shaderCode = &shaderIR.vs;
                shaderSize = &shaderIR.vsSize;
            } else if (shaderIR.shaderType == ShaderType::Fragment) {
                shaderCode = &shaderIR.fs;
                shaderSize = &shaderIR.fsSize;
            }
            shaderCode->assign(static_cast<size_t>(spvByteSize), '\0');
            if (!spvFile.read(shaderCode->data(), spvByteSize)) {
                std::cerr << "Error: failed to read precompiled SPV file: " << spvFilePath << std::endl;
                return false;
            }
            *shaderSize = static_cast<uint32_t>(spvByteSize);

            shaderIRs.push_back(std::move(shaderIR));
        }

        //注册预编译好的 ShaderIR
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
