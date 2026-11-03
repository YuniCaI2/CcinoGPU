#include "CodeGenerator/ASTGenerator.h"
#include "CodeGenerator/SignatureFlattener.h"
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/TypeBase.h>

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

            auto type = field->getType().getNonReferenceType().getCanonicalType().getUnqualifiedType();
            //处理包装器
            if (auto* tmp = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(type->getAsCXXRecordDecl())) {
                //Accessor
                if (tmp->getName() == "Accessor" || tmp->getName() == "accessor") {
                    //得到第一个模板参数     
                    auto& args = tmp->getTemplateArgs();
                    if (args[1].getKind() == clang::TemplateArgument::Declaration) {
                        if (llvm::isa<clang::TemplateParamObjectDecl>(args[1].getAsDecl())) {
                            InsertCXXRecordDeclToGraph(args[0].getAsType().getNonReferenceType().getCanonicalType().getUnqualifiedType()->getAsCXXRecordDecl(), true);
                        }
                    } else {
                        llvm::errs() << "Error: Accessor template need BufferAccessFlags as second argument: "
                                     << tmp->getName() << "\n";
                        throw std::invalid_argument(
                            "Error: Accessor template need BufferAccessFlags as second argument: " + tmp->getNameAsString());
                        return {};
                    }
                    
                } else {
                    //TODO: Texture处理
                    InsertCXXRecordDeclToGraph(type->getAsCXXRecordDecl(), false);
                }
            } else {
                InsertCXXRecordDeclToGraph(type->getAsCXXRecordDecl(), false);
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