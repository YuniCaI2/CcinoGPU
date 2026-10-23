#include "CodeGenerator/CppParser.h"
#include "ShaderStruct/ShaderIR.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_set>

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

    std::vector<Ccino::ShaderIR> shaderIRs = Ccino::CppParser::ParseFilePassShaders(compileCommands.string(), &globalFunctions);
    std::unordered_set<std::string> generatedFiles;
    for (const auto& shaderIR : shaderIRs) {
        if (shaderIR.passName.empty()) {
            std::cerr << "ShaderIR pass name is empty\n";
            return 1;
        }

        const std::filesystem::path outputPath = shaderDir / (shaderIR.passName + ".json");
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
        if (!entry.is_regular_file() || path.extension() != ".json") {
            continue;
        }
        if (generatedFiles.contains(path.filename().string())) {
            continue;
        }

        std::ifstream input(path, std::ios::binary);
        nlohmann::json existing;
        try {
            existing = nlohmann::json::parse(input);
        } catch (const nlohmann::json::exception&) {
            continue;
        }
        if (existing.is_object() && existing.contains("passName") && existing.contains("glsl")) {
            std::filesystem::remove(path, ec);
        }
    }
    if (!globalFunctions.empty()) {
        std::filesystem::path globalHeaderPath = CCINO_GLOBAL_HEADER_FILE;
        std::ofstream outFile(globalHeaderPath, std::ios::binary | std::ios::trunc);
        outFile << globalFunctions;
    }

    return 0;
}
