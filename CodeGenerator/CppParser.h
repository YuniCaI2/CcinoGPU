#pragma once
#include <clang/AST/Decl.h>
#include <clang/AST/Type.h>
#include <vector>
#include <string>
#include <clang/AST/ASTContext.h>
#include <clang/AST/ExprCXX.h>
#include "CodeGenerator/ShaderIR.h"

namespace Ccino {
    class CppParser {
    public:
        static std::vector<std::string> GetAllValidFilePaths(const std::string& projectDir);

        // 外部主入口：运行一次 ClangTool 解析该文件，在 MatchCallback 回调里直接把匹配到的所有 Lambda 转成 ShaderIR 返回
        static std::vector<ShaderIR> ParseFilePassShaders(const std::string& filePath);

        // 内部 AST 处理函数（在 MatchFinder::MatchCallback::run 内部调用，此时 AST 存活）：
        static ShaderIR ProcessLambdaExpr(const clang::LambdaExpr* lambda, clang::ASTContext& context);

        static std::string GenerateBindingInfo();

        // AST 转换工具函数
        static std::string ChangeCppTypeToGLSLType(clang::QualType type);

        static std::string ChangeCppOperatorToGLSLOperator(clang::BinaryOperatorKind op);
        static std::string ChangeCppOperatorToGLSLOperator(clang::OverloadedOperatorKind op);
        static std::string ChangeAccessorToSSBOReference(clang::ValueDecl* value);

        static std::string GetGLSLDefine(clang::QualType type, bool needStructHeader = false);
    };
}