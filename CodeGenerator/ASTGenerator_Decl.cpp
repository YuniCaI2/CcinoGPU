#include "CodeGenerator/ASTGenerator.h"
#include "CodeGenerator/GLSLAST.h"
#include "CodeGenerator/SignatureFlattener.h"
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/TypeBase.h>
#include <clang/AST/ASTContext.h>
#include <llvm/Support/Casting.h>
#include <memory>
#include <utility>
#include <vector>

namespace {
    bool IsBuiltinType(clang::QualType type) {
        auto* record = type.getNonReferenceType().getCanonicalType()->getAsCXXRecordDecl();
        return Ccino::SignatureFlattener::FlattenRecord(record) != Ccino::SignatureFlattener::FlattenType(type).name;
    }
    std::string GetInout(clang::QualType type) {
        if (type->isLValueReferenceType() || type->isPointerType()) {
            return "inout ";
        } else {
            return "";
        } 
    }

    std::vector<std::unique_ptr<Ccino::GLSL::DeclField>> GetFunctionDeclFields(clang::FunctionDecl* funcDecl) {
        if (!funcDecl) return {};
         
        //处理几种情况成员函数和普通函数
        //成员函数
        std::vector<std::unique_ptr<Ccino::GLSL::DeclField>> results;
        if (auto* methodDecl = llvm::dyn_cast<clang::CXXMethodDecl>(funcDecl)) {
            //Get Class Type
            const auto *parent = methodDecl->getParent()->getCanonicalDecl();
            auto &AST = methodDecl->getASTContext();

            clang::QualType classType = AST.getCanonicalTagType(parent); //得到classType
            if (IsBuiltinType(classType)) {
                return {};
            }

            auto selfDeclField = std::make_unique<Ccino::GLSL::DeclField>(); 
            if (classType->getTypeClassName() ) {
                std::string className = Ccino::SignatureFlattener::FlattenType(classType).name;
                selfDeclField->type = "inout " + className;
                selfDeclField->name = "self";
            } else {
                //输出
                llvm::errs() << "Can't Find methodDecl Class Name \n";
                return {};
            }
            results.push_back(std::move(selfDeclField));
        }

        //成员函数其他和普通函数一致
        for (unsigned i = 0; i < funcDecl->getNumParams(); i++) {
            if(auto* decl = funcDecl->getParamDecl(i)) {
                auto field = std::make_unique<Ccino::GLSL::DeclField>();
                auto [typeName, dimensions] = Ccino::SignatureFlattener::FlattenType(decl->getType());
                field->type = GetInout(decl->getType()) + typeName;
                field->name = decl->getNameAsString();
                field->dimensions = std::move(dimensions);
                results.push_back(std::move(field));
            } else {
                llvm::errs() << "Can't find param in: " << Ccino::SignatureFlattener::FlattenFunction(funcDecl) << "\n";
                return {};
            }
        }
        return results;
    }

    //处理依赖
    class DepDeclCollector : public clang::RecursiveASTVisitor<DepDeclCollector> {
    public:
        bool VisitDeclStmt(clang::DeclStmt* declStmt) {
            for (auto* stmt : declStmt->decls()) {
                if (auto* var = llvm::dyn_cast<clang::VarDecl>(stmt)) {
                    auto canonical = var->getType().getNonReferenceType().getCanonicalType().getUnqualifiedType();
                    if (!IsBuiltinType(canonical)) { //不要处理内建变量
                        depDeclNames.push_back(Ccino::SignatureFlattener::FlattenType(canonical).name); 
                    }
                }
            }
            return true;
        }

        std::vector<std::string> depDeclNames;
    };

    std::vector<std::string> GetFuncDep(clang::FunctionDecl* decl) {
        DepDeclCollector collector;
        collector.TraverseStmt(decl->getBody());
        return std::move(collector.depDeclNames);
    }
}

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
        glslFunc->fields = GetFunctionDeclFields(functionDecl);
        glslFunc->name = SignatureFlattener::FlattenFunction(functionDecl);
        glslFunc->returnType = SignatureFlattener::FlattenType(functionDecl->getReturnType()).name;
        glslFunc->depDeclNames = GetFuncDep(functionDecl);
        
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


            m_currentStructDecl = nullptr;
        } else {
            auto* stmt = functionDecl->getBody();
            auto glslBodyStmt = ProcessClangStmt(stmt);
            glslFunc->body = std::move(glslBodyStmt);
        }

        if (glslFunc->body && !glslFunc->name.empty() && !glslFunc->returnType.empty()) {
            InsertGLSLFunc(std::move(glslFunc));
        }
    }

    void ASTGenerator::InsertCXXRecordDeclToGraph(clang::CXXRecordDecl* recordDecl) {
        if (!recordDecl) return;
        const clang::CXXRecordDecl* def = recordDecl->getDefinition();
        if (!def) return;
        auto glslStruct = std::make_unique<GLSL::StructDecl>();
        glslStruct->name = SignatureFlattener::FlattenRecord(recordDecl);
        for (const clang::FieldDecl* field : def->fields()) {
            auto glslField = std::make_unique<GLSL::DeclField>();
            auto [typeName, dimensions] = SignatureFlattener::FlattenType(field->getType());
            glslField->name = field->getNameAsString();
            glslField->type = std::move(typeName);
            glslField->dimensions = std::move(dimensions);
            glslStruct->fields.push_back(std::move(glslField));
        } 
        InsertGLSLStruct(std::move(glslStruct));
    }

    void ASTGenerator::InsertGLSLFunc(std::unique_ptr<GLSL::FunctionDecl>&& funcDecl) {
        auto* rawPtr = funcDecl.get(); //方便先拿出
        m_declGraph.nodes[funcDecl->name] = std::move(funcDecl);    
        std::unordered_set<std::string> ms;
        for (auto& dep : rawPtr->fields) {
            ms.insert(dep->type);
        }
        for (auto& dep : rawPtr->depDeclNames) {
            ms.insert(dep);
        }

        for (auto& dep : ms) {
            m_declGraph.edges[rawPtr->name].push_back(dep); //设置依赖
        }
    }

    void ASTGenerator::InsertGLSLStruct(std::unique_ptr<GLSL::StructDecl>&& structDecl) {
        auto* rawPtr = structDecl.get(); //方便先拿出
        m_declGraph.nodes[structDecl->name] = std::move(structDecl);    
        std::unordered_set<std::string> ms;
        for (auto& dep : rawPtr->fields) {
            ms.insert(dep->type);
        }
        for (auto& dep : ms) {
            m_declGraph.edges[rawPtr->name].push_back(dep); //设置依赖
        }
    }

}