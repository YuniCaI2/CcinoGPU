#include "CppParser.h"
#include "RG/AccessorEnums.h"
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/TemplateBase.h>
#include <clang/AST/Type.h>
#include <clang/Basic/OperatorKinds.h>

namespace Ccino {
    std::vector<std::string> CppParser::GetAllValidFilePaths(const std::string& projectDir) {
        return {};
    }

    // 外部主入口：运行一次 ClangTool 解析该文件，在 MatchCallback 回调里直接把匹配到的所有 Lambda 转成 ShaderIR 返回
    std::vector<ShaderIR> CppParser::ParseFilePassShaders(const std::string& filePath) {
        return {};
    }

    // 内部 AST 处理函数（在 MatchFinder::MatchCallback::run 内部调用，此时 AST 存活）：
    ShaderIR CppParser::ProcessLambdaExpr(const clang::LambdaExpr* lambda, clang::ASTContext& context) {
        return {};
    }

    // AST 转换工具函数
    std::string CppParser::ChangeCppTypeToGLSLType(clang::QualType type) {
        if (type.isNull()) return "";
        //剥离引用(&)、const/volatile，并拿到去糖后的底层真实类型 (穿透 using Vec3 / uint32_t / auto)
        clang::QualType canonical = type.getNonReferenceType().getCanonicalType().getUnqualifiedType();
        //处理基础内置类型 (int, uint32_t, float, double, bool, void)
        if (const auto* builtin = canonical->getAs<clang::BuiltinType>()) {
            switch (builtin->getKind()) {
                case clang::BuiltinType::Void:   return "void";
                case clang::BuiltinType::Bool:   return "bool";
                case clang::BuiltinType::Int:    return "int";
                case clang::BuiltinType::UInt:   return "uint"; // uint32_t 和 unsigned int 在这都会命中
                case clang::BuiltinType::Float:  return "float";
                case clang::BuiltinType::Double: return "double";
                default: break;
            }
        }
        //处理结构体与模板特化 (CcinoType.h 中的 Vec / Mat / Array 以及用户自定义 struct)
        if (const clang::CXXRecordDecl* record = canonical->getAsCXXRecordDecl()) {
            if (const auto* spec = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record)) {
                llvm::StringRef tmplName = spec->getName();
                const clang::TemplateArgumentList& args = spec->getTemplateArgs(); //这里直接得到模板名并且没有命名空间
                //匹配 glm::vec<L, T, Q> -> 自动合成 vec3 / dvec3 / ivec3 / uvec3 / bvec3
                if (tmplName == "vec" && args.size() >= 2) {
                    int64_t len = args[0].getAsIntegral().getExtValue(); // 2, 3, 4
                    clang::QualType elemType = args[1].getAsType();
                    std::string prefix;
                    if (elemType->isBooleanType())                 prefix = "b"; //确认模板参数
                    else if (elemType->isSignedIntegerType())      prefix = "i";
                    else if (elemType->isUnsignedIntegerType())    prefix = "u";
                    else if (elemType->isSpecificBuiltinType(clang::BuiltinType::Double)) prefix = "d";
                    // float 前缀为空
                    return prefix + "vec" + std::to_string(len);
                }
                //匹配 glm::mat<C, R, T, Q> -> 自动合成 mat4 / mat3x4 / dmat4 等
                if (tmplName == "mat" && args.size() >= 3) {
                    int64_t cols = args[0].getAsIntegral().getExtValue(); //转换为int64_t 
                    int64_t rows = args[1].getAsIntegral().getExtValue();
                    clang::QualType elemType = args[2].getAsType();
                    std::string prefix = elemType->isSpecificBuiltinType(clang::BuiltinType::Double) ? "d" : "";
                    if (cols == rows) {
                        return prefix + "mat" + std::to_string(cols); // 如 mat4, dmat3
                    }
                    return prefix + "mat" + std::to_string(cols) + "x" + std::to_string(rows); // 如 mat3x4
                }
                //匹配 Ccino::Array<T, N> (底层为 std::array<T, N>) -> 合成 T[N]
                if (tmplName == "array" && args.size() >= 2) {
                    std::string elemGLSL = ChangeCppTypeToGLSLType(args[0].getAsType()); //递归处理内部类型
                    int64_t count = args[1].getAsIntegral().getExtValue();
                    return elemGLSL + "[" + std::to_string(count) + "]";
                }
            }

            //普通用户自定义结构体（如 struct Particle），直接返回结构体名
            return record->getNameAsString();
        }
        return canonical.getAsString();
    }

    //单独处理Accessor 生成layout(buffer) binding 信息
    std::string CppParser::ChangeAccessorToSSBOReference(clang::ValueDecl* value) {
        std::string ssboBinding = ""; //SSBO 默认使用STD430
        clang::QualType type = value->getType();
        auto canonical = type.getNonReferenceType().getCanonicalType().getUnqualifiedType();
        if (const clang::CXXRecordDecl* record = canonical->getAsCXXRecordDecl()) {
            if (const auto* spec = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record)) {
                llvm::StringRef tmplName = spec->getName();
                const clang::TemplateArgumentList& args = spec->getTemplateArgs(); //这里直接得到模板名并且没有命名空间
                if (tmplName == "Accessor" && args.size() >= 2) {
                    if (args[1].getKind() == clang::TemplateArgument::Declaration) {
                        if (const auto* tpo = llvm::dyn_cast<clang::TemplateParamObjectDecl>(args[1].getAsDecl())) { //NTTP 会被当为TemplateParamObjectDecl
                            const clang::APValue& structVal = tpo->getValue();
                            if (structVal.isStruct()) {
                                auto maskField = structVal.getStructField(0).getInt().getExtValue();
                                if (maskField == static_cast<int64_t>(BufferAccessFlagBits::ReadWrite)) {
                                    ssboBinding = "layout(buffer_reference, std430) readwrite buffer";
                                } else if (maskField == static_cast<int64_t>(BufferAccessFlagBits::Read)) {
                                    ssboBinding = "layout(buffer_reference, std430) readonly buffer";
                                } else if (maskField == static_cast<int64_t>(BufferAccessFlagBits::Write)) {
                                    ssboBinding = "layout(buffer_reference, std430) writeonly buffer";
                                }
                                ssboBinding += " " + GetGLSLDefine(args[0].getAsType(), false);
                                return ssboBinding;
                            }
                        }
                    } else {
                        llvm::errs() << "Error: Accessor template need BufferAccessFlags as second argument: " << tmplName << "\n";
                        return {};
                    }
                }
            }
        }
        return {};
    }

    std::string CppParser::ChangeCppOperatorToGLSLOperator(clang::BinaryOperatorKind op) {
        //仅仅支持简单的操作
        return clang::BinaryOperator::getOpcodeStr(op).str(); //这里把OP_Plus -> + 这种形式
    }

    std::string CppParser::ChangeCppOperatorToGLSLOperator(clang::OverloadedOperatorKind op) {
        if (auto spelling = clang::getOperatorSpelling(op)) {
            return std::string(spelling);
        }
        return {}; //空则不支持此操作
    }

    std::string CppParser::GetGLSLDefine(clang::QualType type, bool needStructHeader) {
        clang::QualType canonical = type.getNonReferenceType().getCanonicalType().getUnqualifiedType();
        std::string glslDefine = "";
        if (const clang::CXXRecordDecl* record = canonical->getAsCXXRecordDecl()) {
            if (const auto def = record->getDefinition()) { //直接得到完整类型，防止隐式类型
                if (needStructHeader) {
                    glslDefine += "struct " + def->getNameAsString() + " {\n";
                } else {
                    glslDefine += def->getNameAsString() + " {\n";
                }

                for (const auto* field : def->fields()) {
                    llvm::StringRef fieldName = field->getName();
                    clang::QualType fieldType = field->getType();
                    std::string fieldGLSL = ChangeCppTypeToGLSLType(fieldType);
                    glslDefine += "    " + fieldGLSL + " " + fieldName.str() + ";\n";
                }
                glslDefine += "};\n";
                return glslDefine;
            }
        }
        return glslDefine;
    }
}