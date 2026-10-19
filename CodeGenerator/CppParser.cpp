#include "CppParser.h"
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>

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
                const clang::TemplateArgumentList& args = spec->getTemplateArgs();
                //匹配 glm::vec<L, T, Q> -> 自动合成 vec3 / dvec3 / ivec3 / uvec3 / bvec3
                if (tmplName == "vec" && args.size() >= 2) {
                    int64_t len = args[0].getAsIntegral().getExtValue(); // 2, 3, 4
                    clang::QualType elemType = args[1].getAsType();
                    std::string prefix;
                    if (elemType->isBooleanType())                 prefix = "b";
                    else if (elemType->isSignedIntegerType())      prefix = "i";
                    else if (elemType->isUnsignedIntegerType())    prefix = "u";
                    else if (elemType->isSpecificBuiltinType(clang::BuiltinType::Double)) prefix = "d";
                    // float 前缀为空
                    return prefix + "vec" + std::to_string(len);
                }
                //匹配 glm::mat<C, R, T, Q> -> 自动合成 mat4 / mat3x4 / dmat4 等
                if (tmplName == "mat" && args.size() >= 3) {
                    int64_t cols = args[0].getAsIntegral().getExtValue();
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
                    std::string elemGLSL = ChangeCppTypeToGLSLType(args[0].getAsType());
                    int64_t count = args[1].getAsIntegral().getExtValue();
                    return elemGLSL + "[" + std::to_string(count) + "]";
                }
            }
            //普通用户自定义结构体（如 struct Particle），直接返回结构体名
            return record->getNameAsString();
        }
        return canonical.getAsString();
    }

    std::string CppParser::ChangeCppOperatorToGLSLOperator(clang::BinaryOperatorKind op) {
        return {};
    }
    std::string CppParser::ChangeCppOperatorToGLSLOperator(clang::OverloadedOperatorKind op) {
        return {};
    }
}