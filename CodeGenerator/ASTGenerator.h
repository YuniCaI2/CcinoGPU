#pragma once
#include "CodeGenerator/GLSLAST.h"
#include "ShaderStruct/ShaderIR.h"
#include "ShaderStruct/UniformData.h"
#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/TypeBase.h>
#include <memory>
#include <unordered_set>

namespace clang {
    class Stmt;
    class DeclStmt;
    class IfStmt;
    class ForStmt;
    class WhileStmt;
    class ReturnStmt;
    class Expr;
    class MemberExpr;
    class CallExpr;
    class IntegerLiteral;
    class CXXBoolLiteralExpr;
    class BinaryOperator;
    class UnaryOperator;
    class ConditionalOperator;
    class FloatingLiteral;
    class ExplicitCastExpr;
    class CXXConstructExpr;
    class InitListExpr;
    class CXXMemberCallExpr;
    class CXXOperatorCallExpr;
    class ExplicitCastExpr;
    class CXXRecordDecl;
    class FunctionDecl;
}

//这个代码处理clangAST生成GLSL AST， GLSL的代码
namespace Ccino {
    class ASTGenerator {
    public:
        std::unique_ptr<GLSL::LoweringStmt> ProcessClangStmt(clang::Stmt* stmt);
        std::unique_ptr<GLSL::LoweringStmt> ProcessClangExpr(clang::Expr* expr);

        //Decl
        void InsertFunctionDeclToGraph(clang::FunctionDecl* functionDecl); //这里包括普通函数和成员函数
        void InsertCXXRecordDeclToGraph(clang::CXXRecordDecl* recordDecl);
        void InsertCXXRecordDeclToGraph(clang::DeclRefExpr* declExpr); //处理Accessor
        std::vector<std::vector<std::string>> SortDeclGraph(); //这里的输出类似RenderGraph的Timeline
        std::string EmitDeclGraph();

        //IR Gen
        DescriptorSlotLayout GetDescriptorSlotLayout(const clang::LambdaExpr* lambdaExpr, clang::ASTContext& context);
        std::vector<ShaderUniformDataBinding> GetUniformDataBindings(const clang::LambdaExpr* lambdaExpr, clang::ASTContext& context);

        //Binding
        std::unique_ptr<GLSL::UniformBinding> GenerateUniformDataReference(const clang::LambdaExpr* lambdaExpr, clang::ASTContext& context); 

        void SetCurrentStructDecl(clang::CXXRecordDecl* decl) { m_currentStructDecl = decl; }
        clang::CXXRecordDecl* GetCurrentStructDecl() const { return m_currentStructDecl; }

    private:
        //Stmt
        std::unique_ptr<GLSL::DeclStmt> ProcessDeclStmt(clang::DeclStmt* stmt);
        std::unique_ptr<GLSL::IfStmt> ProcessIfStmt(clang::IfStmt* stmt);
        std::unique_ptr<GLSL::ForStmt> ProcessForStmt(clang::ForStmt* stmt);
        std::unique_ptr<GLSL::WhileStmt> ProcessWhileStmt(clang::WhileStmt* stmt);
        std::unique_ptr<GLSL::ReturnStmt> ProcessReturnStmt(clang::ReturnStmt* stmt);

        //Expr
        std::unique_ptr<GLSL::LoweringStmt> ProcessCallExpr(clang::CallExpr* expr);
        std::unique_ptr<GLSL::LoweringStmt> ProcessMemberCallExpr(clang::CXXMemberCallExpr* expr);
        std::unique_ptr<GLSL::LoweringStmt> ProcessOperatorCallExpr(clang::CXXOperatorCallExpr* expr);
        std::unique_ptr<GLSL::LoweringStmt> ProcessMemberExpr(clang::MemberExpr* expr);
        std::unique_ptr<GLSL::LoweringStmt> ProcessILExpr(clang::IntegerLiteral* expr);
        std::unique_ptr<GLSL::LoweringStmt> ProcessBinaryLiteral(clang::CXXBoolLiteralExpr* expr);
        std::unique_ptr<GLSL::LoweringStmt> ProcessBinaryOp(clang::BinaryOperator* expr);
        std::unique_ptr<GLSL::LoweringStmt> ProcessUnaryOp(clang::UnaryOperator* expr);
        std::unique_ptr<GLSL::LoweringStmt> ProcessCondOp(clang::ConditionalOperator* expr);
        std::unique_ptr<GLSL::LoweringStmt> ProcessFloatLiteral(clang::FloatingLiteral* expr);
        std::unique_ptr<GLSL::LoweringStmt> ProcessExplicitCast(clang::ExplicitCastExpr* expr);
        std::unique_ptr<GLSL::LoweringStmt> ProcessConstruct(clang::CXXConstructExpr* expr);
        std::unique_ptr<GLSL::LoweringStmt> ProcessInitList(clang::InitListExpr* expr);


        //Tool
        std::unordered_set<std::string> GetStructMember(clang::CXXRecordDecl* decl);
        void InsertGLSLFunc(std::unique_ptr<GLSL::FunctionDecl>&& funcDecl);
        void InsertGLSLStruct(std::unique_ptr<GLSL::StructDecl>&& structDecl);

        //IR Gen
        UniformDataType GetUniformDataType(clang::QualType type);

        static std::string GenerateNumString();

        GLSL::DeclGraph m_declGraph;
        std::unique_ptr<GLSL::Stmt> m_root;

        //当前状态记录, 记录当前处理的指针是否属于成员
        clang::CXXRecordDecl* m_currentStructDecl = nullptr;
    };
}