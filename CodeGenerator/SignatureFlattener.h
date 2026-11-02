#pragma once
#include <clang/AST/Type.h>
#include <string>

namespace clang {
    class NamedDecl;
    class FunctionDecl;
    class CXXMethodDecl;
    class RecordDecl;
    class TemplateArgument;
    class TemplateArgumentList;
}

namespace Ccino {
    struct FlattenedType {
        std::string name;
        std::string dimensions;
    };

    // 将 C++ 带有命名空间、嵌套类、模板实参及参数列表的作用域签名展平为合法的 GLSL 标识符
    class SignatureFlattener {
    public:
        // 展平任意具名声明：Namespace_OuterStruct_InnerStruct_Name
        static std::string Flatten(const clang::NamedDecl* decl);

        // 展平函数/成员函数签名；includeParams 为 true 时追加参数类型后缀以区分重载
        static std::string FlattenFunction(const clang::FunctionDecl* func, bool includeParams = true);

        // 展平结构体/类声明：Namespace_OuterStruct_StructName
        static std::string FlattenRecord(const clang::RecordDecl* record);

        // 展平类型为合法的基础类型名与数组维度（如 {"float", "[2][4]"}）
        static FlattenedType FlattenType(clang::QualType type);

    private:
        static std::string FlattenTypeAsIdentifier(clang::QualType type);
        static std::string FormatDeclOwnName(const clang::NamedDecl* decl);
        static void AppendTemplateArgs(const clang::TemplateArgumentList& args, std::string& out);
        static void AppendTemplateArg(const clang::TemplateArgument& arg, std::string& out);
        static void AppendSegment(std::string& out, std::string_view segment);
        static std::string SanitizeGLSLIdentifier(std::string_view raw);
    };
}
