#include "CodeGenerator/CppParser.h"
#include "GLSLCompiler/GLSLCompiler.h"
#include "ShaderStruct/ShaderIR.h"

#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

int main() {
    const std::filesystem::path compileCommands(CCINO_COMPILE_COMMAND_JSON_PATH);
    if (!std::filesystem::exists(compileCommands)) {
        std::cerr << "Compilation database not found: " << compileCommands << "\n";
        return 1;
    }

    const std::filesystem::path shaderDir(CCINO_SHADER_DIR);
    std::error_code ec;
    std::filesystem::create_directories(shaderDir, ec);
    if (ec) {
        std::cerr << "Failed to create shader directory: " << shaderDir << "\n";
        return 1;
    }

    std::string globalFunctions = {};

    std:: cout << "Generating GLSL Code..." << std::endl;
    std::vector<Ccino::ShaderIR> shaderIRs = Ccino::CppParser::ParseFilePassShaders(compileCommands.string(), &globalFunctions);
    if (!globalFunctions.empty()) {
        std::filesystem::path globalHeaderPath = CCINO_GLOBAL_HEADER_FILE;
        std::ofstream outFile(globalHeaderPath, std::ios::binary | std::ios::trunc);
        outFile << globalFunctions;
    }

    std::unordered_set<std::string> generatedFiles;
    auto compileStage = [&](const std::string& passName, const std::string& glsl, Ccino::ShaderType shaderType,
                            const std::filesystem::path& spvPath, std::string& spvPathOut, uint32_t& spvSizeOut) {
        try {
            const std::vector<uint32_t> spv = Ccino::GLSLCompiler::CompileToSpv(passName, glsl, shaderType);
            spvSizeOut = static_cast<uint32_t>(spv.size() * sizeof(uint32_t));

            std::ofstream spvStream(spvPath, std::ios::binary | std::ios::trunc);
            if (!spvStream) {
                std::cerr << "Failed to open SPV file for writing: " << spvPath << "\n";
                return false;
            }
            spvStream.write(reinterpret_cast<const char*>(spv.data()), static_cast<std::streamsize>(spvSizeOut));
            if (!spvStream) {
                std::cerr << "Failed to write SPV file: " << spvPath << "\n";
                return false;
            }
        } catch (const std::exception& e) {
            std::cerr << "Failed to compile GLSL to SPV for pass '" << passName << "':\n" << e.what() << "\n";
            return false;
        }
        spvPathOut = spvPath.generic_string();
        generatedFiles.insert(spvPath.filename().string());
        return true;
    };

    for (auto& shaderIR : shaderIRs) {
        if (shaderIR.passName.empty()) {
            std::cerr << "ShaderIR pass name is empty\n";
            return 1;
        }

        if (shaderIR.passType == Ccino::PassType::Compute) {
            const auto it = shaderIR.cs.find("in;");
            if (it == std::string::npos) {
                std::cerr << "Error: the compute shader doesn't have thread group size: " << shaderIR.passName << "\n";
                return 1;
            }
        }

        const std::filesystem::path outputPath = shaderDir / (shaderIR.passName + ".json");
        if (shaderIR.passType == Ccino::PassType::Compute) {
            if (!compileStage(shaderIR.passName, shaderIR.cs, Ccino::ShaderType::Compute,
                              shaderDir / (shaderIR.passName + ".spv"), shaderIR.csSpvPath, shaderIR.csSize)) {
                return 1;
            }
        } else {
            if (!compileStage(shaderIR.passName, shaderIR.vs, Ccino::ShaderType::Vertex,
                              shaderDir / (shaderIR.passName + ".vs.spv"), shaderIR.vsSpvPath, shaderIR.vsSize) ||
                !compileStage(shaderIR.passName, shaderIR.fs, Ccino::ShaderType::Fragment,
                              shaderDir / (shaderIR.passName + ".fs.spv"), shaderIR.fsSpvPath, shaderIR.fsSize)) {
                return 1;
            }
        }
        std::cout << " GLSL Compiling : " << shaderIR.passName << std::endl;

        std::ofstream os(outputPath, std::ios::binary | std::ios::trunc);
        if (!os) {
            std::cerr << "Failed to write " << outputPath << "\n";
            return 1;
        }
        os << nlohmann::json(shaderIR).dump(4) << '\n';
        if (!os) {
            std::cerr << "Failed to write " << outputPath << "\n";
            return 1;
        }

        generatedFiles.insert(outputPath.filename().string());
    }

    for (const auto& entry : std::filesystem::directory_iterator(shaderDir)) {
        const std::filesystem::path& path = entry.path();
        if (!entry.is_regular_file()) {
            continue;
        }
        if (generatedFiles.contains(path.filename().string())) {
            continue;
        }
        if (path.extension() == ".spv") {
            std::filesystem::remove(path, ec);
            continue;
        }
        if (path.extension() != ".json") {
            continue;
        }

        std::ifstream input(path, std::ios::binary);
        nlohmann::json existing;
        try {
            existing = nlohmann::json::parse(input);
        } catch (const nlohmann::json::exception&) {
            continue;
        }
        if (existing.is_object() && existing.contains("passName") &&
            (existing.contains("cs") || existing.contains("glsl"))) {
            std::filesystem::remove(path, ec);
        }
    }

    return 0;
}
