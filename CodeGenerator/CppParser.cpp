#include "CppParser.h"
#include "RG/AccessorEnums.h"
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/RecordLayout.h>
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
        auto canonical = type.getNonReferenceType().getCanonicalType().getUnqualifiedType(); //去掉引用 去掉别名 去掉cv
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


    std::string CppParser::ChangeCppExprToGLSLExpr(clang::Expr* expr) {
        expr = expr->IgnoreUnlessSpelledInSource(); //剔除重复的代码
        if (auto* DRE = llvm::dyn_cast<clang::DeclRefExpr>(expr)) { //只有一个值时走,引用表达式
            return DRE->getDecl()->getNameAsString(); 
        } 
        
        if (auto* Op = llvm::dyn_cast<clang::CXXOperatorCallExpr>(expr)) {
            std::string op = clang::getOperatorSpelling(Op->getOperator());

            if (Op->getOperator() == clang::OO_PlusPlus ||
                Op->getOperator() == clang::OO_MinusMinus) {
                const clang::Expr* operand = Op->getArg(0);
                std::string operandGLSL = ChangeCppExprToGLSLExpr(const_cast<clang::Expr*>(operand));

                return Op->getNumArgs() == 2 //如果为2 则为后缀 如果为1 则为前缀
                    ? "(" + operandGLSL + op + ")"
                    : "(" + op + operandGLSL + ")";
            }

            if (Op->isInfixBinaryOp()) { //中缀
                return "("
                    + ChangeCppExprToGLSLExpr(Op->getArg(0))
                    + " " + op + " "
                    + ChangeCppExprToGLSLExpr(Op->getArg(1))
                    + ")";
            }

            if (Op->getNumArgs() == 1) { //前缀
                // -a、!a、~a 等前缀一元运算符
                return "(" + op + ChangeCppExprToGLSLExpr(Op->getArg(0)) + ")";
            }
        }
        
        if (auto* Op = llvm::dyn_cast<clang::BinaryOperator>(expr)) {
            std::string op = Op->getOpcodeStr().str();
            return "("
                + ChangeCppExprToGLSLExpr(Op->getLHS())
                + " " + op + " "
                + ChangeCppExprToGLSLExpr(Op->getRHS())
                + ")";
        }
        
        //浮点数字面量
        if (auto* FL = llvm::dyn_cast<clang::FloatingLiteral>(expr)) {
            llvm::SmallString<16> S;
            FL->getValue().toString(S);
            return S.str().str();
        }
        
        //构造函数
        if (auto* Cstr = llvm::dyn_cast<clang::CXXConstructExpr>(expr)) { //调用构造函数
            std::string R = ChangeCppTypeToGLSLType(Cstr->getType()) + "("; 
            for (int i = 0; i < Cstr->getNumArgs()(); ++i) {
                R += (i ? "," : "") + ChangeCppExprToGLSLExpr(Cstr->getArg(i)); 
            }
            return R + ")";
        }
        
        //函数调用侧
        if (auto *Call = llvm::dyn_cast<clang::CallExpr>(expr))             // glm::dot(a,b) -> dot(a,b)
            if (auto *FD = Call->getDirectCallee()) {
                std::string R = FD->getName().str() + "(";
                for (unsigned i = 0; i < Call->getNumArgs(); ++i)
                    R += (i ? ", " : "") + ChangeCppExprToGLSLExpr(Call->getArg(i));
                return R + ")";
            }
        
        llvm::errs() << "Error: Accessor template need BufferAccessFlags as second argument: " + std::string(expr->getStmtClassName()) + "\n"; 
        return {}; //直接返回空
    }

    std::string CppParser::GenerateGlobalFunctionToGLSL(clang::FunctionDecl* functionDecl) {

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

    UniformDataType CppParser::GetUniformDataType(clang::QualType type) {
        if (type.isNull()) {
            throw std::invalid_argument("QualType is null");
        }

        clang::QualType canonical = type.getNonReferenceType().getCanonicalType().getUnqualifiedType();

        // 1. 基础内置标量类型 (bool, int, uint32_t, uint64_t, float, double)
        if (const auto* builtin = canonical->getAs<clang::BuiltinType>()) {
            switch (builtin->getKind()) {
                case clang::BuiltinType::Bool:      return UniformDataType::Bool;
                case clang::BuiltinType::Int:       return UniformDataType::Int;
                case clang::BuiltinType::UInt:      return UniformDataType::UInt;
                case clang::BuiltinType::ULong:
                case clang::BuiltinType::ULongLong: return UniformDataType::Buffer; // uint64_t (BDA)
                case clang::BuiltinType::Float:     return UniformDataType::Float;
                case clang::BuiltinType::Double:    return UniformDataType::Double;
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
                    } else if (elemType->isSignedIntegerType()) {
                        switch (len) {
                            case 2: return UniformDataType::IVec2;
                            case 3: return UniformDataType::IVec3;
                            case 4: return UniformDataType::IVec4;
                        }
                    } else if (elemType->isUnsignedIntegerType()) {
                        switch (len) {
                            case 2: return UniformDataType::UVec2;
                            case 3: return UniformDataType::UVec3;
                            case 4: return UniformDataType::UVec4;
                        }
                    } else if (elemType->isSpecificBuiltinType(clang::BuiltinType::Float)) {
                        switch (len) {
                            case 2: return UniformDataType::Vec2;
                            case 3: return UniformDataType::Vec3;
                            case 4: return UniformDataType::Vec4;
                        }
                    } else if (elemType->isSpecificBuiltinType(clang::BuiltinType::Double)) {
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
                        case 22: return isDouble ? UniformDataType::DMat2   : UniformDataType::Mat2;
                        case 23: return isDouble ? UniformDataType::DMat2x3 : UniformDataType::Mat2x3;
                        case 24: return isDouble ? UniformDataType::DMat2x4 : UniformDataType::Mat2x4;
                        case 32: return isDouble ? UniformDataType::DMat3x2 : UniformDataType::Mat3x2;
                        case 33: return isDouble ? UniformDataType::DMat3   : UniformDataType::Mat3;
                        case 34: return isDouble ? UniformDataType::DMat3x4 : UniformDataType::Mat3x4;
                        case 42: return isDouble ? UniformDataType::DMat4x2 : UniformDataType::Mat4x2;
                        case 43: return isDouble ? UniformDataType::DMat4x3 : UniformDataType::Mat4x3;
                        case 44: return isDouble ? UniformDataType::DMat4   : UniformDataType::Mat4;
                    }
                }
            }
        }

        throw std::invalid_argument("Unsupported QualType for UniformDataType: " + canonical.getAsString());
    }

    std::vector<ShaderUniformDataBinding> CppParser::GetUniformDataBindings(clang::LambdaExpr* lambdaExpr, clang::ASTContext& context) {
        std::vector<ShaderUniformDataBinding> uniformDataBindings;

        const clang::CXXRecordDecl* lambdaClass = lambdaExpr->getLambdaClass();
        const clang::ASTRecordLayout& lambdaClassLayout = context.getASTRecordLayout(lambdaClass);

        uint32_t fieldIndex = 0;

        for (const auto* field : lambdaClass->fields()) {

            std::string varName = field->getNameAsString();
            uint32_t offset = static_cast<uint32_t>(lambdaClassLayout.getFieldOffset(fieldIndex) / 8);
            UniformDataType uniformDataType = GetUniformDataType(field->getType()); //QualType转换为uniformData对应的类型
            uniformDataBindings.push_back(ShaderUniformDataBinding{varName, offset, uniformDataType});
            fieldIndex++;
        }

        return uniformDataBindings;
    }


    bool CppParser::IsGlobalVariable(clang::ValueDecl* value) {
        if (const auto* varDecl = llvm::dyn_cast<clang::VarDecl>(value)) { //值声明变为变量声明
            // 1. 判断是否在文件/命名空间作用域声明（真正的全局变量 + 类的 static 静态成员变量）
            bool isGlobalScopeVar = varDecl->isFileVarDecl();

            // 2. 判断是否具有全局存储期（包含上面的全局变量 + 函数内部的 static 局部变量）
            bool hasGlobalStorage = varDecl->hasGlobalStorage();

            // 3. 判断是否为函数内的普通局部变量（不含函数参数）
            bool isLocalVar = varDecl->isLocalVarDecl();

            return isGlobalScopeVar && hasGlobalStorage && !isLocalVar;
        }
        return false;
    }

    DescriptorSlotLayout CppParser::GetDescriptorSlotLayout(clang::LambdaExpr* lambdaExpr, clang::ASTContext& context) {
        DescriptorSlotLayout descriptorSlotLayout;
        const clang::CXXRecordDecl* lambdaClass = lambdaExpr->getLambdaClass();

        for (const auto* field : lambdaClass->fields()) {

            std::string varName = field->getNameAsString();
            UniformDataType uniformDataType = GetUniformDataType(field->getType()); //QualType转换为uniformData对应的类型
            descriptorSlotLayout.emplace_back(varName, uniformDataType);

        }
        return descriptorSlotLayout;
    }
}