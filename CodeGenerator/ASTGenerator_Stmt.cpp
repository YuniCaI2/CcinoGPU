#include "CodeGenerator/ASTGenerator.h"
#include "CodeGenerator/GLSLAST.h"
#include "CodeGenerator/SignatureFlattener.h"
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/StmtCXX.h>
#include <clang/AST/Type.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/raw_ostream.h>
#include <memory>
#include <string>
#include <utility>

namespace Ccino {
    namespace {
        bool IsTrivialEmptyInit(clang::Expr* init) {
            if (!init) return true;
            clang::Expr* stripped = init->IgnoreUnlessSpelledInSource();
            if (!stripped) return true;

            if (auto* cstr = llvm::dyn_cast<clang::CXXConstructExpr>(stripped)) {
                return cstr->getNumArgs() == 0;
            }

            if (auto* ile = llvm::dyn_cast<clang::InitListExpr>(stripped)) {
                if (auto* semantic = ile->getSemanticForm()) {
                    ile = semantic;
                }
                for (clang::Expr* elem : ile->inits()) {
                    if (elem && !llvm::isa<clang::ImplicitValueInitExpr>(elem)) {
                        return false;
                    }
                }
                return true;
            }

            return false;
        }
    }

    std::unique_ptr<GLSL::LoweringStmt> ASTGenerator::ProcessClangStmt(clang::Stmt* stmt) {
        if (!stmt) return {};

        if (auto* compoundStmt = llvm::dyn_cast<clang::CompoundStmt>(stmt)) {
            auto lowering = std::make_unique<GLSL::LoweringStmt>();
            auto compound = std::make_unique<GLSL::CompoundStmt>();
            for (clang::Stmt* subStmt : compoundStmt->body()) {
                if (auto subLowering = ProcessClangStmt(subStmt)) {
                    compound->fields.push_back(std::move(subLowering));
                }
            }
            lowering->fields.push_back(std::move(compound));
            return lowering;
        }

        if (auto* ifStmt = llvm::dyn_cast<clang::IfStmt>(stmt)) {
            auto glslIf = ProcessIfStmt(ifStmt);
            if (!glslIf) return {};
            auto lowering = std::make_unique<GLSL::LoweringStmt>();
            lowering->fields.push_back(std::move(glslIf));
            return lowering;
        }

        if (auto* forStmt = llvm::dyn_cast<clang::ForStmt>(stmt)) {
            auto glslFor = ProcessForStmt(forStmt);
            if (!glslFor) return {};
            auto lowering = std::make_unique<GLSL::LoweringStmt>();
            lowering->fields.push_back(std::move(glslFor));
            return lowering;
        }

        if (auto* whileStmt = llvm::dyn_cast<clang::WhileStmt>(stmt)) {
            auto glslWhile = ProcessWhileStmt(whileStmt);
            if (!glslWhile) return {};
            auto lowering = std::make_unique<GLSL::LoweringStmt>();
            lowering->fields.push_back(std::move(glslWhile));
            return lowering;
        }

        if (auto* returnStmt = llvm::dyn_cast<clang::ReturnStmt>(stmt)) {
            auto glslReturn = ProcessReturnStmt(returnStmt);
            if (!glslReturn) return {};
            auto lowering = std::make_unique<GLSL::LoweringStmt>();
            lowering->fields.push_back(std::move(glslReturn));
            return lowering;
        }

        if (llvm::isa<clang::BreakStmt>(stmt)) {
            auto lowering = std::make_unique<GLSL::LoweringStmt>();
            lowering->fields.push_back(std::make_unique<GLSL::BreakStmt>());
            return lowering;
        }

        if (llvm::isa<clang::ContinueStmt>(stmt)) {
            auto lowering = std::make_unique<GLSL::LoweringStmt>();
            lowering->fields.push_back(std::make_unique<GLSL::ContinueStmt>());
            return lowering;
        }

        if (llvm::isa<clang::NullStmt>(stmt)) {
            auto lowering = std::make_unique<GLSL::LoweringStmt>();
            lowering->fields.push_back(std::make_unique<GLSL::NullStmt>());
            return lowering;
        }

        if (auto* declStmt = llvm::dyn_cast<clang::DeclStmt>(stmt)) {
            auto glslDecl = ProcessDeclStmt(declStmt);
            if (!glslDecl) return {};
            auto lowering = std::make_unique<GLSL::LoweringStmt>();
            lowering->fields.push_back(std::move(glslDecl));
            return lowering;
        }

        if (auto* expr = llvm::dyn_cast<clang::Expr>(stmt)) {
            return ProcessClangExpr(expr);
        }

        llvm::errs() << "Error: Unsupported Stmt in ASTGenerator: " << stmt->getStmtClassName() << "\n";
        return {};
    }

    std::unique_ptr<GLSL::DeclStmt> ASTGenerator::ProcessDeclStmt(clang::DeclStmt* stmt) {
        if (!stmt) return {};

        auto result = std::make_unique<GLSL::DeclStmt>();

        for (clang::Decl* decl : stmt->decls()) {
            auto* var = llvm::dyn_cast<clang::VarDecl>(decl);
            if (!var) {
                llvm::errs() << "Error: unsupported declaration in DeclStmt: " << decl->getDeclKindName() << "\n";
                return {};
            }

            const std::string name = var->getNameAsString();
            if (name.empty()) return {};

            clang::QualType canonical = var->getType().getNonReferenceType().getCanonicalType().getUnqualifiedType();
            if (const clang::CXXRecordDecl* record = canonical->getAsCXXRecordDecl()) {
                if (const auto* spec = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record)) {
                    if (spec->getName() == "Accessor") {
                        llvm::errs() << "Error: Accessor cannot be declared inside shader function body: " << name
                                     << "\n";
                        return {};
                    }
                }
            }

            //lambda 体里的局部结构体由 CppParser 的 LambdaInnerCollector 收集, 被递归翻译的函数体只能在这里收集
            if (isInGlobalFunctionBody) {
                InsertRecordTypeToGraph(canonical);
            }

            auto [typeName, dimensions] = SignatureFlattener::FlattenType(canonical);
            if (typeName.empty()) return {};

            GLSL::VarDecl glslVar;
            glslVar.type = std::move(typeName);
            glslVar.name = name;
            glslVar.dimensions = std::move(dimensions);

            if (clang::Expr* init = var->getInit()) {
                if (!IsTrivialEmptyInit(init)) {
                    auto initLowering = ProcessClangExpr(init);
                    if (!initLowering || !initLowering->expr || initLowering->expr->text.empty()) {
                        return {};
                    }
                    glslVar.initStmt = std::move(initLowering);
                }
            }

            result->decls.push_back(std::move(glslVar));
        }

        return result;
    }

    std::unique_ptr<GLSL::IfStmt> ASTGenerator::ProcessIfStmt(clang::IfStmt* stmt) {
        if (!stmt) return {};
        if (stmt->getInit()) {
            llvm::errs() << "GLSL does not support if init-statement\n";
            return {};
        }
        if (stmt->getConditionVariable()) {
            llvm::errs() << "GLSL does not support condition-variable declaration in If STMT\n";
            return {};
        }
        std::unique_ptr<GLSL::IfStmt> result = std::make_unique<GLSL::IfStmt>();
        if (auto* condExpr = stmt->getCond()) {
            result->condStmt = ProcessClangExpr(condExpr);
            if (!result->condStmt) return {};
        }
        else {
            return {};
        }
        auto* thenStmt = stmt->getThen();
        auto* elseStmt = stmt->getElse();


        if (thenStmt) {
            result->thenStmt = ProcessClangStmt(thenStmt);
            if (!result->thenStmt) return {};
        }
        else {
            llvm::errs() << "GLSL does not support Non if then-statement\n";
            return {};
        }

        if (elseStmt) {
            result->elseStmt = ProcessClangStmt(elseStmt);
            if (!result->elseStmt) return {};
        }

        return result;
    }

    std::unique_ptr<GLSL::ForStmt> ASTGenerator::ProcessForStmt(clang::ForStmt* stmt) {
        if (!stmt) return {};
        if (llvm::dyn_cast<clang::CXXForRangeStmt>(stmt)) {
            llvm::errs() << "GLSL does not support range-for declaration\n";
            return {};
        }

        if (stmt->getConditionVariableDeclStmt()) {
            llvm::errs() << "GLSL does not support condition-variable declaration\n";
            return {};
        }

        auto result = std::make_unique<GLSL::ForStmt>();

        if (auto* initStmt = stmt->getInit()) {
            result->initStmt = ProcessClangStmt(initStmt);
            if (!result->initStmt) return {};
        }

        if (auto* condExpr = stmt->getCond()) {
            result->condStmt = ProcessClangExpr(condExpr);
            if (!result->condStmt) return {};
        }

        if (auto* incStmt = stmt->getInc()) {
            result->incStmt = ProcessClangStmt(incStmt);
            if (!result->incStmt) return {};
        }

        if (auto* body = stmt->getBody()) {
            result->body = ProcessClangStmt(body);
            if (!result->body) return {};
        }

        return result;
    }

    std::unique_ptr<GLSL::WhileStmt> ASTGenerator::ProcessWhileStmt(clang::WhileStmt* stmt) {
        if (!stmt) return {};
        if (stmt->getConditionVariable()) {
            // 例如 while (int x = f())，第一版可不支持
            llvm::errs() << "GLSL does not support condition-variable declaration in While STMT\n";
            return {};
        }
        auto result = std::make_unique<GLSL::WhileStmt>();
        result->condStmt = ProcessClangExpr(stmt->getCond());
        result->body = ProcessClangStmt(stmt->getBody());
        if (!result->condStmt || !result->body) {
            return {};
        }

        return result;
    }

    std::unique_ptr<GLSL::ReturnStmt> ASTGenerator::ProcessReturnStmt(clang::ReturnStmt* stmt) {
        if (!stmt) return {};
        auto result = std::make_unique<GLSL::ReturnStmt>();
        if (auto* retExpr = stmt->getRetValue()) {
            result->returnStmt = ProcessClangExpr(retExpr);
            if (!result->returnStmt) return {};
        }
        return result;
    }
}
