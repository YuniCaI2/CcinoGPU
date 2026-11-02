#include "CodeGenerator/ASTGenerator.h"
#include <clang/AST/RecordLayout.h>

namespace Ccino {
    DescriptorSlotLayout ASTGenerator::GetDescriptorSlotLayout(const clang::LambdaExpr* lambdaExpr, clang::ASTContext& context) {
        DescriptorSlotLayout descriptorSlotLayout;
        const clang::CXXRecordDecl* lambdaClass = lambdaExpr->getLambdaClass();
        uint32_t fieldIndex = 0;

        for (const auto* field : lambdaClass->fields()) {
            std::string varName = field->getNameAsString();
            if (varName.empty() && fieldIndex < lambdaClass->capture_size()) {
                const auto* capture = lambdaClass->getCapture(fieldIndex);
                if (capture && capture->capturesVariable()) {
                    varName = capture->getCapturedVar()->getNameAsString();
                }
            }
            UniformDataType uniformDataType =
                GetUniformDataType(field->getType());  // QualType转换为uniformData对应的类型
            descriptorSlotLayout.emplace_back(varName, uniformDataType);
            fieldIndex++;
        }
        return descriptorSlotLayout;
    }

    std::vector<ShaderUniformDataBinding> ASTGenerator::GetUniformDataBindings(const clang::LambdaExpr* lambdaExpr, clang::ASTContext& context) {
        std::vector<ShaderUniformDataBinding> uniformDataBindings;

        const clang::CXXRecordDecl* lambdaClass = lambdaExpr->getLambdaClass();
        const clang::ASTRecordLayout& lambdaClassLayout = context.getASTRecordLayout(lambdaClass);

        uint32_t fieldIndex = 0;

        for (const auto* field : lambdaClass->fields()) {
            std::string varName = field->getNameAsString();
            if (varName.empty() && fieldIndex < lambdaClass->capture_size()) {
                const auto* capture = lambdaClass->getCapture(fieldIndex);
                if (capture && capture->capturesVariable()) {
                    varName = capture->getCapturedVar()->getNameAsString();
                }
            }
            uint32_t offset = static_cast<uint32_t>(lambdaClassLayout.getFieldOffset(fieldIndex) / 8);
            UniformDataType uniformDataType =
                GetUniformDataType(field->getType());  // QualType转换为uniformData对应的类型
            uniformDataBindings.push_back(ShaderUniformDataBinding {varName, offset, uniformDataType});
            fieldIndex++;
        }

        return uniformDataBindings;
    }

    UniformDataType ASTGenerator::GetUniformDataType(clang::QualType type) {
        if (type.isNull()) {
            throw std::invalid_argument("QualType is null");
        }

        clang::QualType canonical = type.getNonReferenceType().getCanonicalType().getUnqualifiedType();

        // 1. 基础内置标量类型 (bool, int, uint32_t, uint64_t, float, double)
        if (const auto* builtin = canonical->getAs<clang::BuiltinType>()) {
            switch (builtin->getKind()) {
                case clang::BuiltinType::Bool: return UniformDataType::Bool;
                case clang::BuiltinType::Int: return UniformDataType::Int;
                case clang::BuiltinType::UInt: return UniformDataType::UInt;
                case clang::BuiltinType::ULong: return UniformDataType::UInt;  // Windows 上unsigned long 是32位
                case clang::BuiltinType::ULongLong: return UniformDataType::UInt64;  // uint64_t 只是普通整数参数, Buffer 语义来自下面的Accessor
                case clang::BuiltinType::Float: return UniformDataType::Float;
                case clang::BuiltinType::Double: return UniformDataType::Double;
                default: break;
            }
        }

        // 2. 模板特化类型 (Accessor / glm::vec / glm::mat)
        if (const clang::CXXRecordDecl* record = canonical->getAsCXXRecordDecl()) {
            if (const auto* spec = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record)) {
                llvm::StringRef tmplName = spec->getName();
                const clang::TemplateArgumentList& args = spec->getTemplateArgs();

                // Accessor<T, Flags> -> Buffer (BDA)
                if (tmplName == "Accessor") {
                    return UniformDataType::Buffer;
                }

                // glm::vec<L, T, Q>
                if (tmplName == "vec" && args.size() >= 2) {
                    int64_t len = args[0].getAsIntegral().getExtValue();
                    clang::QualType elemType = args[1].getAsType();

                    if (elemType->isBooleanType()) {
                        switch (len) {
                            case 2: return UniformDataType::BVec2;
                            case 3: return UniformDataType::BVec3;
                            case 4: return UniformDataType::BVec4;
                        }
                    }
                    else if (elemType->isSignedIntegerType()) {
                        switch (len) {
                            case 2: return UniformDataType::IVec2;
                            case 3: return UniformDataType::IVec3;
                            case 4: return UniformDataType::IVec4;
                        }
                    }
                    else if (elemType->isUnsignedIntegerType()) {
                        switch (len) {
                            case 2: return UniformDataType::UVec2;
                            case 3: return UniformDataType::UVec3;
                            case 4: return UniformDataType::UVec4;
                        }
                    }
                    else if (elemType->isSpecificBuiltinType(clang::BuiltinType::Float)) {
                        switch (len) {
                            case 2: return UniformDataType::Vec2;
                            case 3: return UniformDataType::Vec3;
                            case 4: return UniformDataType::Vec4;
                        }
                    }
                    else if (elemType->isSpecificBuiltinType(clang::BuiltinType::Double)) {
                        switch (len) {
                            case 2: return UniformDataType::DVec2;
                            case 3: return UniformDataType::DVec3;
                            case 4: return UniformDataType::DVec4;
                        }
                    }
                }

                // glm::mat<C, R, T, Q>
                if (tmplName == "mat" && args.size() >= 3) {
                    int64_t cols = args[0].getAsIntegral().getExtValue();
                    int64_t rows = args[1].getAsIntegral().getExtValue();
                    clang::QualType elemType = args[2].getAsType();
                    bool isDouble = elemType->isSpecificBuiltinType(clang::BuiltinType::Double);

                    switch (cols * 10 + rows) {
                        case 22: return isDouble ? UniformDataType::DMat2 : UniformDataType::Mat2;
                        case 23: return isDouble ? UniformDataType::DMat2x3 : UniformDataType::Mat2x3;
                        case 24: return isDouble ? UniformDataType::DMat2x4 : UniformDataType::Mat2x4;
                        case 32: return isDouble ? UniformDataType::DMat3x2 : UniformDataType::Mat3x2;
                        case 33: return isDouble ? UniformDataType::DMat3 : UniformDataType::Mat3;
                        case 34: return isDouble ? UniformDataType::DMat3x4 : UniformDataType::Mat3x4;
                        case 42: return isDouble ? UniformDataType::DMat4x2 : UniformDataType::Mat4x2;
                        case 43: return isDouble ? UniformDataType::DMat4x3 : UniformDataType::Mat4x3;
                        case 44: return isDouble ? UniformDataType::DMat4 : UniformDataType::Mat4;
                    }
                }
            }
        }

        throw std::invalid_argument("Unsupported QualType for UniformDataType: " + canonical.getAsString());
    }
}