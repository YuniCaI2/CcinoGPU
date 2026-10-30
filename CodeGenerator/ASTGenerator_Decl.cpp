#include "CodeGenerator/ASTGenerator.h"
#include "CodeGenerator/GLSLAST.h"
#include "CodeGenerator/SignatureFlattener.h"
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <llvm/Support/Casting.h>
#include <memory>
#include <utility>

namespace Ccino {
    void ASTGenerator::InsertFunctionDeclToGraph(clang::FunctionDecl* functionDecl) {
        if(!functionDecl) return;
        auto* definition = functionDecl->getDefinition(); //得到其定义

        if (!definition) {
            llvm::errs() << "Can't Find Function Decl Name : " << functionDecl->getNameAsString() << "\n";
            return ;
        }
        if (!definition->hasBody()) {
            llvm::errs() << "Can't Find Function Body : " << functionDecl->getNameAsString() << "\n";
            return ;
        }

        auto glslFunc = std::make_unique<GLSL::FunctionDecl>();
        //首先判断当前的function是否是member

        
        if (auto* memberFuncDecl = llvm::dyn_cast<clang::CXXMethodDecl>(functionDecl)) {
            //这里是类函数所有这里的函数需要调整
            //这里特殊处理构造和析构 
            /*
            构造：
            auto a = A(b);

            GLSL:
            A _temp;
            A a = ctor_A(_temp, b); //这里的_temp 输入是inout ,所有这里的Decl 构造和析构可以直接一样处理即可
            */


            std::string name = SignatureFlattener::FlattenFunction(memberFuncDecl, true);
            auto* parent = memberFuncDecl->getParent()->getCanonicalDecl(); //这里是实例化的结果
            if (!parent) return; //这里没有父类直接撤回

            m_currentStructDecl = parent; //这里记录下当前的函数是
            auto* stmt = memberFuncDecl->getBody();
            auto glslBodyStmt = ProcessClangStmt(stmt);
            glslFunc->body = std::move(glslBodyStmt);

            //TODO:处理Texture的Viewer
        }

    }

    void ASTGenerator::InsertCXXRecordDeclToGraph(clang::CXXRecordDecl* recordDecl) {

    }
}