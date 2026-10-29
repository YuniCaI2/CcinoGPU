#include "CodeGenerator/ASTGenerator.h"
#include <clang/AST/Stmt.h>
#include <memory>

namespace Ccino {
    std::unique_ptr<GLSL::LoweringStmt> ASTGenerator::ProcessClangStmt(clang::Stmt* stmt) {
        return {};
    }

    std::unique_ptr<GLSL::LoweringStmt> ASTGenerator::ProcessIfStmt(clang::Stmt* stmt) {
        return {};
    }

    std::unique_ptr<GLSL::LoweringStmt> ASTGenerator::ProcessForStmt(clang::Stmt* stmt) {
        return {};
    }

    std::unique_ptr<GLSL::LoweringStmt> ASTGenerator::ProcessWhileStmt(clang::Stmt* stmt) {
        return {};
    }

    std::unique_ptr<GLSL::LoweringStmt> ASTGenerator::ProcessReturnStmt(clang::Stmt* stmt) {
        return {};
    }
}
