#include "CodeGenerator/SignatureFlattener.h"
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/TemplateBase.h>
#include <clang/Basic/OperatorKinds.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/Support/Casting.h>
#include <cctype>
#include <cstdint>
#include <string_view>
#include <vector>

namespace Ccino {
    namespace {
        std::string OverloadedOperatorToIdentifier(clang::OverloadedOperatorKind op) {
            switch (op) {
                case clang::OO_Plus:                return "op_plus";
                case clang::OO_Minus:               return "op_minus";
                case clang::OO_Star:                return "op_mul";
                case clang::OO_Slash:               return "op_div";
                case clang::OO_Percent:             return "op_mod";
                case clang::OO_Caret:               return "op_xor";
                case clang::OO_Amp:                 return "op_bitand";
                case clang::OO_Pipe:                return "op_bitor";
                case clang::OO_Tilde:               return "op_bitnot";
                case clang::OO_Exclaim:             return "op_not";
                case clang::OO_Equal:               return "op_assign";
                case clang::OO_Less:                return "op_lt";
                case clang::OO_Greater:             return "op_gt";
                case clang::OO_PlusEqual:           return "op_add_assign";
                case clang::OO_MinusEqual:          return "op_sub_assign";
                case clang::OO_StarEqual:           return "op_mul_assign";
                case clang::OO_SlashEqual:          return "op_div_assign";
                case clang::OO_PercentEqual:        return "op_mod_assign";
                case clang::OO_CaretEqual:          return "op_xor_assign";
                case clang::OO_AmpEqual:            return "op_and_assign";
                case clang::OO_PipeEqual:           return "op_or_assign";
                case clang::OO_LessLess:            return "op_shl";
                case clang::OO_GreaterGreater:      return "op_shr";
                case clang::OO_LessLessEqual:       return "op_shl_assign";
                case clang::OO_GreaterGreaterEqual: return "op_shr_assign";
                case clang::OO_EqualEqual:          return "op_eq";
                case clang::OO_ExclaimEqual:        return "op_ne";
                case clang::OO_LessEqual:           return "op_le";
                case clang::OO_GreaterEqual:        return "op_ge";
                case clang::OO_AmpAmp:              return "op_and";
                case clang::OO_PipePipe:            return "op_or";
                case clang::OO_PlusPlus:            return "op_inc";
                case clang::OO_MinusMinus:          return "op_dec";
                case clang::OO_Subscript:           return "op_subscript";
                case clang::OO_Call:                return "op_call";
                default:                            return "op_" + std::to_string(static_cast<int>(op));
            }
        }

        bool IsInNamespace(const clang::Decl* decl, llvm::StringRef targetNs) {
            for (const clang::DeclContext* ctx = decl ? decl->getDeclContext() : nullptr;
                 ctx;
                 ctx = ctx->getParent()) {
                if (const auto* ns = llvm::dyn_cast<clang::NamespaceDecl>(ctx)) {
                    if (ns->getName() == targetNs) return true;
                }
            }
            return false;
        }
    }

    std::string SignatureFlattener::Flatten(const clang::NamedDecl* decl) {
        if (!decl) return {};

        std::vector<std::string> segments;
        for (const clang::DeclContext* ctx = decl->getDeclContext(); ctx; ctx = ctx->getParent()) {
            if (ctx->isTranslationUnit()) break;

            if (const auto* ns = llvm::dyn_cast<clang::NamespaceDecl>(ctx)) {
                if (ns->isAnonymousNamespace() || ns->getName().empty()) continue;
                segments.push_back(ns->getNameAsString());
                continue;
            }

            if (const auto* record = llvm::dyn_cast<clang::RecordDecl>(ctx)) {
                if (record->isAnonymousStructOrUnion()) continue;
                if (const auto* cxxRecord = llvm::dyn_cast<clang::CXXRecordDecl>(record)) {
                    if (cxxRecord->isLambda()) continue;
                }
                std::string recName = FormatDeclOwnName(record);
                if (!recName.empty()) {
                    segments.push_back(std::move(recName));
                }
                continue;
            }

            if (const auto* func = llvm::dyn_cast<clang::FunctionDecl>(ctx)) {
                if (const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(func)) {
                    if (method->getParent() && method->getParent()->isLambda()) continue;
                }
                std::string fnName = FormatDeclOwnName(func);
                if (!fnName.empty()) {
                    segments.push_back(std::move(fnName));
                }
            }
        }

        std::string result;
        for (auto it = segments.rbegin(); it != segments.rend(); ++it) {
            AppendSegment(result, *it);
        }
        AppendSegment(result, FormatDeclOwnName(decl));
        return result;
    }

    std::string SignatureFlattener::FlattenFunction(const clang::FunctionDecl* func, bool includeParams) {
        if (!func) return {};

        std::string result = Flatten(func);
        if (!includeParams) return result;

        for (const clang::ParmVarDecl* param : func->parameters()) {
            if (!param) continue;
            AppendSegment(result, FlattenType(param->getType()));
        }

        if (const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(func)) {
            if (method->isConst()) {
                AppendSegment(result, "const");
            }
        }
        return result;
    }

    std::string SignatureFlattener::FlattenRecord(const clang::RecordDecl* record) {
        return Flatten(record);
    }

    std::string SignatureFlattener::FlattenType(clang::QualType type) {
        if (type.isNull()) return {};

        clang::QualType canonical = type.getNonReferenceType().getCanonicalType().getUnqualifiedType();

        // 1. 基础内置标量类型
        if (const auto* builtin = canonical->getAs<clang::BuiltinType>()) {
            switch (builtin->getKind()) {
                case clang::BuiltinType::Void:      return "void";
                case clang::BuiltinType::Bool:      return "bool";
                case clang::BuiltinType::Short:
                case clang::BuiltinType::Int:
                case clang::BuiltinType::Long:      return "int";
                case clang::BuiltinType::UShort:
                case clang::BuiltinType::UInt:
                case clang::BuiltinType::ULong:     return "uint";
                case clang::BuiltinType::LongLong:  return "int64_t";
                case clang::BuiltinType::ULongLong: return "uint64_t";
                case clang::BuiltinType::Float:     return "float";
                case clang::BuiltinType::Double:    return "double";
                default: break;
            }
        }

        // 2. C 原生定长数组 T[N]
        if (const auto* arr = llvm::dyn_cast<clang::ConstantArrayType>(canonical.getTypePtr())) {
            std::string out = FlattenType(arr->getElementType());
            AppendSegment(out, std::to_string(arr->getSize().getZExtValue()));
            return out;
        }

        // 3. 结构体与模板特化（内置数学类型 / 容器 / 用户自定义 struct）
        if (const auto* record = canonical->getAsCXXRecordDecl()) {
            if (const auto* spec = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record)) {
                llvm::StringRef tmplName = spec->getName();
                const clang::TemplateArgumentList& args = spec->getTemplateArgs();

                // glm::vec<L, T, Q> -> vec3 / ivec3 / uvec3 / bvec3 / dvec3
                if (tmplName == "vec" && IsInNamespace(spec, "glm") && args.size() >= 2 &&
                    args[0].getKind() == clang::TemplateArgument::Integral &&
                    args[1].getKind() == clang::TemplateArgument::Type) {
                    int64_t len = args[0].getAsIntegral().getExtValue();
                    clang::QualType elemType = args[1].getAsType();
                    std::string prefix;
                    if (elemType->isBooleanType()) prefix = "b";
                    else if (elemType->isSignedIntegerType()) prefix = "i";
                    else if (elemType->isUnsignedIntegerType()) prefix = "u";
                    else if (elemType->isSpecificBuiltinType(clang::BuiltinType::Double)) prefix = "d";
                    return prefix + "vec" + std::to_string(len);
                }

                // glm::mat<C, R, T, Q> -> mat4 / mat3x4 / dmat4
                if (tmplName == "mat" && IsInNamespace(spec, "glm") && args.size() >= 3 &&
                    args[0].getKind() == clang::TemplateArgument::Integral &&
                    args[1].getKind() == clang::TemplateArgument::Integral &&
                    args[2].getKind() == clang::TemplateArgument::Type) {
                    int64_t cols = args[0].getAsIntegral().getExtValue();
                    int64_t rows = args[1].getAsIntegral().getExtValue();
                    clang::QualType elemType = args[2].getAsType();
                    std::string prefix = elemType->isSpecificBuiltinType(clang::BuiltinType::Double) ? "d" : "";
                    if (cols == rows) {
                        return prefix + "mat" + std::to_string(cols);
                    }
                    return prefix + "mat" + std::to_string(cols) + "x" + std::to_string(rows);
                }

                // std::array<T, N> / Ccino::Array<T, N>
                if (tmplName == "array" && IsInNamespace(spec, "std") && args.size() >= 2 &&
                    args[0].getKind() == clang::TemplateArgument::Type &&
                    args[1].getKind() == clang::TemplateArgument::Integral) {
                    std::string out = FlattenType(args[0].getAsType());
                    AppendSegment(out, std::to_string(args[1].getAsIntegral().getExtValue()));
                    return out;
                }

                // Ccino::Accessor<T, Flags>
                if (tmplName == "Accessor" && IsInNamespace(spec, "Ccino") && args.size() >= 1 &&
                    args[0].getKind() == clang::TemplateArgument::Type) {
                    return FlattenType(args[0].getAsType());
                }
            }

            return FlattenRecord(record);
        }

        return SanitizeGLSLIdentifier(canonical.getAsString());
    }

    std::string SignatureFlattener::FormatDeclOwnName(const clang::NamedDecl* decl) {
        if (!decl) return {};

        std::string ownName;
        if (const auto* func = llvm::dyn_cast<clang::FunctionDecl>(decl)) {
            if (llvm::isa<clang::CXXConstructorDecl>(func)) {
                ownName = "ctor";
            } else if (func->isOverloadedOperator()) {
                ownName = OverloadedOperatorToIdentifier(func->getOverloadedOperator());
            } else {
                ownName = func->getNameAsString();
            }

            if (const auto* tmplArgs = func->getTemplateSpecializationArgs()) {
                AppendTemplateArgs(*tmplArgs, ownName);
            }
            return SanitizeGLSLIdentifier(ownName);
        }

        ownName = decl->getNameAsString();
        if (const auto* spec = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(decl)) {
            AppendTemplateArgs(spec->getTemplateArgs(), ownName);
        }
        return SanitizeGLSLIdentifier(ownName);
    }

    void SignatureFlattener::AppendTemplateArgs(const clang::TemplateArgumentList& args, std::string& out) {
        for (unsigned i = 0; i < args.size(); ++i) {
            AppendTemplateArg(args[i], out);
        }
    }

    void SignatureFlattener::AppendTemplateArg(const clang::TemplateArgument& arg, std::string& out) {
        switch (arg.getKind()) {
            case clang::TemplateArgument::Type:
                AppendSegment(out, FlattenType(arg.getAsType()));
                break;
            case clang::TemplateArgument::Integral: {
                llvm::SmallString<32> intStr;
                arg.getAsIntegral().toString(intStr, 10);
                std::string s = intStr.str().str();
                if (!s.empty() && s.front() == '-') {
                    s.replace(0, 1, "neg_");
                }
                AppendSegment(out, s);
                break;
            }
            case clang::TemplateArgument::Declaration:
                AppendSegment(out, Flatten(arg.getAsDecl()));
                break;
            case clang::TemplateArgument::Pack:
                for (const auto& packArg : arg.pack_elements()) {
                    AppendTemplateArg(packArg, out);
                }
                break;
            default:
                break;
        }
    }

    void SignatureFlattener::AppendSegment(std::string& out, std::string_view segment) {
        std::string clean = SanitizeGLSLIdentifier(segment);
        if (clean.empty()) return;
        if (!out.empty()) {
            if (out.back() != '_' && clean.front() != '_') {
                out.push_back('_');
            } else if (out.back() == '_' && clean.front() == '_') {
                clean.erase(clean.begin());
            }
        }
        out += clean;
    }

    std::string SignatureFlattener::SanitizeGLSLIdentifier(std::string_view raw) {
        std::string result;
        result.reserve(raw.size());

        for (unsigned char ch : raw) {
            const char c = (std::isalnum(ch) || ch == '_') ? static_cast<char>(ch) : '_';
            // GLSL 规范保留所有包含连续双下划线 "__" 的标识符，此处折叠为单下划线
            if (c == '_' && (result.empty() || result.back() == '_')) {
                continue;
            }
            result.push_back(c);
        }

        while (!result.empty() && result.back() == '_') {
            result.pop_back();
        }
        return result;
    }
}
