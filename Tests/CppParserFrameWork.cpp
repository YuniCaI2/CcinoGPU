#include "Tests/CppParserFrameWork.h"
#include "RHI/RHIStruct.h"
#include "ShaderStruct/ShaderIR.h"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>


std::string CppParserTest::GetGLSLCode(const std::string& passName, const std::string& shaderType) {
    //这里需要直接解析Json比较合适
    std::string ccinoShaderDir = CCINO_SHADER_DIR;
    std::filesystem::path ccinoShaderPath = std::filesystem::path(ccinoShaderDir) / (passName + ".json");
    std::ifstream ifs(ccinoShaderPath);
    if (!ifs.is_open()) {
        return {};
    }
    auto shaderIRJson = nlohmann::json::parse(ifs);
    auto shaderIR = shaderIRJson.get<Ccino::ShaderIR>();
    if (shaderType == "cs") {
        return shaderIR.cs;
    } else if (shaderType == "vs") {
        return shaderIR.vs;
    } else if (shaderType == "fs") {
        return shaderIR.fs;
    } else {
        return {};
    }
}


void CppParserTest::SetUpTestSuite() {
    RHISettings settings;
    m_context.Init(settings);
}
