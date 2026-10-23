#include"GLSLCompiler/GLSLCompiler.h"
#include "SPIRV/GlslangToSpv.h"
#include "ShaderStruct/ShaderCommon.h"

#include <iostream>
#include <glslang/Include/ResourceLimits.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>

namespace Ccino {
    std::vector<uint32_t> CompileToSpv(const std::string& passName, const std::string& glsl, ShaderType shaderType) {

        static bool isGlslangInit = false;
        if (!isGlslangInit) {
            glslang::InitializeProcess();
            isGlslangInit = true;
        }

        EShLanguage language;
        switch (shaderType) {
            case Ccino::ShaderType::Vertex:   language = EShLangVertex;   break;
            case Ccino::ShaderType::Fragment: language = EShLangFragment; break;
            case Ccino::ShaderType::Compute:  language = EShLangCompute;  break;
            default:
                std::cerr << std::format("Unsupported shader stage for: {}", passName);
                throw std::runtime_error(std::format("Unsupported shader stage for: {}", passName));
        }

        glslang::TShader shader(language);
        const char* shaderStrings[1] = { glsl.data() };
        int shaderLengths[1] = { static_cast<int>(glsl.size()) };
        shader.setStringsWithLengths(shaderStrings, shaderLengths, 1);

        shader.setEnvInput(glslang::EShSourceGlsl, language, glslang::EShClientVulkan, 100);
        shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_4);
        shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_6);

        EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);
        if (!shader.parse(GetDefaultResources(), 100, false, messages)) {
            std::cerr << std::format("GLSL Parsing Failed for: {}\n{}\n{}", passName, shader.getInfoLog(), shader.getInfoDebugLog());
            throw std::runtime_error(std::format("GLSL Parsing Failed for: {}\n{}\n{}\n--- Generated GLSL ---\n{}",
                passName, shader.getInfoLog(), shader.getInfoDebugLog(), glsl));
        }

        glslang::TProgram program;
        program.addShader(&shader);
        if (!program.link(messages)) {
            std::cerr << std::format("GLSL Linking Failed for: {}\n{}\n{}", passName, program.getInfoLog(), program.getInfoDebugLog());
            throw std::runtime_error(std::format("GLSL Linking Failed for: {}\n{}\n{}",
                passName, program.getInfoLog(), program.getInfoDebugLog()));
        }

        std::vector<uint32_t> spirv;
        glslang::GlslangToSpv(*program.getIntermediate(language), spirv);
        return spirv;
    }
}