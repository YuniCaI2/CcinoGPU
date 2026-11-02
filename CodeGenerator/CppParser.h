#pragma once
#include <clang/AST/ASTContext.h>
#include <clang/AST/ExprCXX.h>
#include <string>
#include <vector>
#include "ShaderStruct/ShaderIR.h"

namespace Ccino
{
    class CppParser
    {
    public:
        // 外部主入口：运行一次 ClangTool 解析该文件，在 MatchCallback 回调里直接把匹配到的所有 Lambda 转成 ShaderIR 返回
        static std::vector<ShaderIR> ParseFilePassShaders(const std::string& compileCommandJsonPath, std::string* globalFunctions = nullptr);

        // 内部 AST 处理函数（在 MatchFinder::MatchCallback::run 内部调用，此时 AST 存活）：
        static ShaderIR ProcessComputeShader(const clang::LambdaExpr* lambda, clang::ASTContext& context, const std::string& passName, ThreadGroupSize threadGroupSize);

    private:
        static std::string GenerateSSBOBinding(const clang::LambdaExpr* lambdaExpr, clang::ASTContext& context);
        static std::string ChangeAccessorToSSBOReference(const clang::ValueDecl* value);
        static std::string GetGLSLDefine(clang::QualType type, bool needStructHeader = false);
    };
}
