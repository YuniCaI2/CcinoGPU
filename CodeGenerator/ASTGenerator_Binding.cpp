#include "CodeGenerator/ASTGenerator.h"
#include "CodeGenerator/SignatureFlattener.h"

namespace Ccino {
    std::unique_ptr<GLSL::UniformBinding> ASTGenerator::GenerateUniformDataReference(const clang::LambdaExpr* lambdaExpr, clang::ASTContext& context) {

        auto result = std::make_unique<GLSL::UniformBinding>();
        uint32_t fieldIndex = 0;
        const clang::CXXRecordDecl* lambdaClass = lambdaExpr->getLambdaClass();
        for (const auto* field : lambdaClass->fields()) {
            auto declField = std::make_unique<GLSL::DeclField>();
            declField->name = field->getNameAsString();
            /*
            普通变量捕获（如 [a, &b] 或 [=] 隐式捕获）： Clang 在生成闭包类的 FieldDecl 时，
            不会给这个 FieldDecl 设置名字（它的 IdentifierInfo* 是 nullptr），所以 field->getNameAsString() 
            返回的一定是空字符串 ""。此时必须通过字段下标 fieldIndex 去查对应的 
            LambdaCapture（lambdaClass->getCapture(fieldIndex)），
            再通过 capture->getCapturedVar()->getNameAsString() 拿到原变量名。
            */
            if (declField->name.empty() && fieldIndex < lambdaClass->capture_size()) {
                const auto* capture = lambdaClass->getCapture(fieldIndex);
                if (capture && capture->capturesVariable()) {
                    declField->name = capture->getCapturedVar()->getNameAsString();
                }
            }
            
            auto flattenType = SignatureFlattener::FlattenType(field->getType());
            declField->type = flattenType.name;
            declField->dimensions = flattenType.dimensions;
            fieldIndex++;
            result->fields.push_back(std::move(declField));
        }
        return result;
    }
}