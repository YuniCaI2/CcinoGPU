#pragma once
#include<vector>
#include<string>
#include"ShaderStruct/ShaderCommon.h"

namespace Ccino {
    class GLSLCompiler {
    public:
        static std::vector<uint32_t> CompileToSpv(const std::string& passName, const std::string& glsl, ShaderType shaderType);
    private:
    };

}