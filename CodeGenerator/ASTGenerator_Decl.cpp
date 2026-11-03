#include "CodeGenerator/ASTGenerator.h"
#include "CodeGenerator/GLSLAST.h"
#include "CodeGenerator/SignatureFlattener.h"
#include "Core/CcinoType.h"
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/TypeBase.h>
#include <clang/AST/ASTContext.h>
#include <llvm/Support/Casting.h>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>
#include <queue>

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

        bool VisitCallExpr(clang::CallExpr* callExpr) {
            if (auto* callee = callExpr->getDirectCallee()) {
                depDeclNames.push_back(Ccino::SignatureFlattener::FlattenFunction(callee));
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
        std::string rawName = functionDecl->getNameAsString();
        std::string lowerName = rawName;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        std::string camelName = rawName;
        if (!camelName.empty()) {
            camelName[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(camelName[0])));
        }
        if (GLSLBuiltinFuncSet.contains(rawName) ||
            GLSLBuiltinFuncSet.contains(lowerName) ||
            GLSLBuiltinFuncSet.contains(camelName)) {
            return; //直接返回
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
            if (IsBuiltinType(parent->getASTContext().getCanonicalTagType(parent))) return; //内建变量的函数通通不实现


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

    void ASTGenerator::InsertCXXRecordDeclToGraph(clang::CXXRecordDecl* recordDecl, bool isBufferRef) {
        if (!recordDecl) return;
        const clang::CXXRecordDecl* def = recordDecl->getDefinition(); //得到定义
        if (!def) return;


        auto& astCtx = def->getASTContext();
        if (IsBuiltinType(astCtx.getCanonicalTagType(def))) return;

        auto glslStruct = std::make_unique<GLSL::StructDecl>();
        glslStruct->name = SignatureFlattener::FlattenRecord(recordDecl);
        glslStruct->isBufferReference = isBufferRef;
        for (const clang::FieldDecl* field : def->fields()) {
            auto glslField = std::make_unique<GLSL::DeclField>();
            auto [typeName, dimensions] = SignatureFlattener::FlattenType(field->getType());
            glslField->name = field->getNameAsString();
            glslField->type = std::move(typeName);
            glslField->dimensions = std::move(dimensions);
            glslStruct->fields.push_back(std::move(glslField));
            //递归处理所有结构体（包含定长数组与 std::array 元素中的嵌套结构体）
            clang::QualType elemType = field->getType().getNonReferenceType().getCanonicalType().getUnqualifiedType();

            //找到嵌套数组最内部的数
            while (const auto* arr = llvm::dyn_cast<clang::ConstantArrayType>(elemType.getTypePtr())) {
                elemType = arr->getElementType().getNonReferenceType().getCanonicalType().getUnqualifiedType();
            }
            if (auto* record = elemType->getAsCXXRecordDecl()) {
                if (const auto* spec = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record)) {
                    const auto& args = spec->getTemplateArgs();

                    //特殊处理array
                    if (spec->getName() == "array" && args.size() >= 1 &&
                        args[0].getKind() == clang::TemplateArgument::Type) {
                        record = args[0].getAsType().getNonReferenceType().getCanonicalType().getUnqualifiedType()->getAsCXXRecordDecl();
                    }
                }
                InsertCXXRecordDeclToGraph(record);
            }
        } 
        InsertGLSLStruct(std::move(glslStruct));
    }

    std::string ASTGenerator::EmitDeclGraph() {
        std::string result;
        auto timeline = SortDeclGraph();
        for (const auto& layer : timeline) {
            for (const auto& nodeName : layer) {
                auto it = m_declGraph.nodes.find(nodeName);
                if (it != m_declGraph.nodes.end() && it->second) {
                    result += it->second->EmitGLSL() + "\n";
                }
            }
        }
        return result;
    }

    //Kahn 排序
    std::vector<std::vector<std::string>> ASTGenerator::SortDeclGraph() {
        std::vector<std::vector<std::string>> result;
        std::unordered_map<std::string, uint32_t> inDegree;
        std::unordered_map<std::string, std::vector<std::string>> dependents; // dep -> 依赖它的节点

        // 初始化所有图内节点的入度为 0
        for (const auto& [name, _] : m_declGraph.nodes) {
            inDegree[name] = 0;
        }

        //只统计在 nodes 中真实存在的自定义类型/函数依赖
        for (const auto& [name, deps] : m_declGraph.edges) {
            if (!m_declGraph.nodes.contains(name)) continue;
            for (std::string dep : deps) {
                if (dep.starts_with("inout ")) {
                    dep.erase(0, 6);
                }
                if (dep == name || !m_declGraph.nodes.contains(dep)) {
                    continue; // 跳过 float/vec3 等内置类型及自环
                }
                dependents[dep].push_back(name);
                inDegree[name]++;
            }
        }

        //收集第 0 层（无任何自定义依赖的节点）
        std::vector<std::string> currentLayer;
        for (const auto& [name, deg] : inDegree) {
            if (deg == 0) {
                currentLayer.push_back(name);
            }
        }

        //按层拓扑推进
        while (!currentLayer.empty()) {
            std::vector<std::string> nextLayer;
            for (const auto& readyNode : currentLayer) {
                for (const auto& userNode : dependents[readyNode]) {
                    if (--inDegree[userNode] == 0) {
                        nextLayer.push_back(userNode);
                    }
                }
            }
            result.push_back(std::move(currentLayer));
            currentLayer = std::move(nextLayer);
        }

        return result;
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

        m_declGraph.edges[rawPtr->name].clear();
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
        m_declGraph.edges[rawPtr->name].clear();
        for (auto& dep : ms) {
            m_declGraph.edges[rawPtr->name].push_back(dep); //设置依赖
        }
    }

}