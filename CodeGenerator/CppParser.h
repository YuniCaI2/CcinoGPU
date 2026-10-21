#pragma once
#include <clang/AST/Decl.h>
#include <clang/AST/Type.h>
#include <vector>
#include <string>
#include <clang/AST/ASTContext.h>
#include <clang/AST/ExprCXX.h>
#include "CodeGenerator/ShaderIR.h"

namespace Ccino
{
    class CppParser
    {
    public:
        static std::vector<std::string> GetAllValidFilePaths(const std::string& projectDir);

        // 外部主入口：运行一次 ClangTool 解析该文件，在 MatchCallback 回调里直接把匹配到的所有 Lambda 转成 ShaderIR 返回
        static std::vector<ShaderIR> ParseFilePassShaders(const std::string& filePath);

        // 内部 AST 处理函数（在 MatchFinder::MatchCallback::run 内部调用，此时 AST 存活）：
        static ShaderIR ProcessLambdaExpr(const clang::LambdaExpr* lambda, clang::ASTContext& context);

        static std::string GenerateBindingInfo();

        // AST 转换工具函数
        static std::string ChangeCppTypeToGLSLType(clang::QualType type);
        static std::string ProcessSTMT(clang::Stmt* stmt); //处理语句

        static std::string ChangeCppOperatorToGLSLOperator(clang::BinaryOperatorKind op);
        static std::string ChangeCppOperatorToGLSLOperator(clang::OverloadedOperatorKind op);
        static std::string ChangeAccessorToSSBOReference(clang::ValueDecl* value);
        static std::string ChangeCppInitToGLSLInitializer(const clang::VarDecl* var);
        static std::string ChangeCppDeclToGLSLDecl(clang::DeclStmt* declStmt);
        static std::string GenerateGlobalFunctionToGLSL(clang::FunctionDecl* functionDecl);
        //把全局函数统一翻译成GLSL函数放到统一的GLSL头文件，保证所有Pass可以共享函数

        static std::string GetGLSLDefine(clang::QualType type, bool needStructHeader = false);
        static UniformDataType GetUniformDataType(clang::QualType type);
        static std::vector<ShaderUniformDataBinding> GetUniformDataBindings(
            clang::LambdaExpr* lambdaExpr, clang::ASTContext& context);
        static DescriptorSlotLayout GetDescriptorSlotLayout(clang::LambdaExpr* lambdaExpr, clang::ASTContext& context);

        static bool IsGlobalVariable(clang::ValueDecl* value);

    private:
        //属于STMT的子类
        //翻译常见语句
        static std::string ChangeCppExprToGLSLExpr(clang::Expr* expr); //这里遇到函数不会处理
        static std::string ChangeCppExprListToGLSLExprList(llvm::ArrayRef<const clang::Expr*> exprs);
        static std::string ChangeCppReturnToGLSLReturn(clang::ReturnStmt* returnStmt);
        static std::string ChangeCppIfToGLSLIf(clang::IfStmt* ifStmt);
        static std::string ChangeCppForToGLSLFor(clang::ForStmt* forStmt);
        static std::string ChangeCppWhileToGLSLWhile(clang::WhileStmt* whileStmt);
    };
}