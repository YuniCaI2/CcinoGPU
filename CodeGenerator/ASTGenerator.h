#pragma once
#include "CodeGenerator/GLSLAST.h"
#include <unordered_set>

namespace clang {
    class Stmt;
    class Expr;
    class MemberExpr;
    class CallExpr;
    class CXXMemberCallExpr;
    class CXXOperatorCallExpr;
    class CXXRecordDecl;
}

//这个代码处理clangAST生成GLSL AST， GLSL的代码
namespace Ccino {
    class ASTGenerator {
    public:
        std::unique_ptr<GLSL::LoweringStmt> ProcessClangStmt(clang::Stmt* stmt);
        std::unique_ptr<GLSL::LoweringStmt> ProcessClangExpr(clang::Expr* expr);

        void SetCurrentStructDecl(clang::CXXRecordDecl* decl) { m_currentStructDecl = decl; }
        clang::CXXRecordDecl* GetCurrentStructDecl() const { return m_currentStructDecl; }

    private:
        //Stmt
        std::unique_ptr<GLSL::LoweringStmt> ProcessIfStmt(clang::Stmt* stmt);
        std::unique_ptr<GLSL::LoweringStmt> ProcessForStmt(clang::Stmt* stmt);
        std::unique_ptr<GLSL::LoweringStmt> ProcessWhileStmt(clang::Stmt* stmt);
        std::unique_ptr<GLSL::LoweringStmt> ProcessReturnStmt(clang::Stmt* stmt);

        //Expr
        std::unique_ptr<GLSL::LoweringStmt> ProcessCallExpr(clang::CallExpr* expr);
        std::unique_ptr<GLSL::LoweringStmt> ProcessMemberCallExpr(clang::CXXMemberCallExpr* expr);
        std::unique_ptr<GLSL::LoweringStmt> ProcessOperatorCallExpr(clang::CXXOperatorCallExpr* expr);
        std::unique_ptr<GLSL::LoweringStmt> ProcessMemberExpr(clang::MemberExpr* expr);


        //Tool
        std::unordered_set<std::string> GetStructMember(clang::CXXRecordDecl* decl);

        GLSL::DeclGraph m_declGraph;
        std::unique_ptr<GLSL::Stmt> m_root;

        //当前状态记录, 记录当前处理的指针是否属于成员
        clang::CXXRecordDecl* m_currentStructDecl = nullptr;
    };
}