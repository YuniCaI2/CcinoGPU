#include "CppParser.h"
#include "CodeGenerator/ShaderIR.h"
#include "Core/CcinoType.h"
#include "RG/AccessorEnums.h"
#include <algorithm>
#include <cctype>
#include <clang/AST/APValue.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/RecordLayout.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/TemplateBase.h>
#include <clang/AST/Type.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/Basic/OperatorKinds.h>
#include <format>
#include <llvm/Support/Casting.h>
#include <llvm/Support/raw_ostream.h>
#include <regex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace {
    class PassShaderCollector : public clang::ast_matchers::MatchFinder::MatchCallback {
    public:
        void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override {
            //先只处理compute Lambda 图形后续再处理
            const auto* csLambda      = result.Nodes.getNodeAs<clang::LambdaExpr>("csLambda");
            const auto* addPassMethod = result.Nodes.getNodeAs<clang::CXXMethodDecl>("addPassMethod");
            const auto* addCSMethod   = result.Nodes.getNodeAs<clang::CXXMethodDecl>("addCSMethod");

            if (!csLambda || !addPassMethod || !addCSMethod || !result.Context) {
                return;
            }

            //需要保证Lambda定义的文件在主要文件中
            if (!result.SourceManager->isWrittenInMainFile(csLambda->getBeginLoc())) {
                return;
            }
            std::string passName = ExtractPassName(addPassMethod);
            Ccino::ThreadGroupSize threadGroupSize = ExtractGroupSize(addCSMethod);
            // 趁 ASTContext 存活，直接生成 ShaderIR
            shaderIRs.push_back(Ccino::CppParser::ProcessComputeShader(
                csLambda, *result.Context, passName, threadGroupSize));
            
        }

        std::string ExtractPassName(const clang::CXXMethodDecl* passMethod) {
            std::string result = "Invalid Name";
            const auto* args = passMethod->getTemplateSpecializationArgs();
            const clang::TemplateArgument& arg0 = args->get(0);

            if (arg0.getKind() != clang::TemplateArgument::StructuralValue) {
                llvm::errs() << "Can't Find Lambda In AddPass! \n";
                return {};
            }

            //这里获取FixString
            const clang::APValue& structVal = arg0.getAsStructuralValue();
            if (!structVal.isStruct() || structVal.getStructNumFields() == 0) return {};

            const clang::APValue& charArray = structVal.getStructField(0);
            if (!charArray.isArray()) return {};

            uint32_t len = charArray.getArrayInitializedElts(); //需要确认
            for (unsigned i = 0; i < len; ++i) {
                char c = static_cast<char>(charArray.getArrayInitializedElt(i).getInt().getZExtValue());
                if (c == '\0') break; // 遇到末尾 '\0' 停止
                result.push_back(c);
            }

            return result;
        }
        
        Ccino::ThreadGroupSize ExtractGroupSize(const clang::CXXMethodDecl* passMethod) { //成员函数声明
            Ccino::ThreadGroupSize size = {1, 1, 1};
            const auto* args = passMethod->getTemplateSpecializationArgs();
            const clang::TemplateArgument& arg0 = args->get(0);

            if (arg0.getKind() != clang::TemplateArgument::StructuralValue) {
                llvm::errs() << "Can't Find Lambda In AddPass! \n";
                return size;
            }

            //这里获取编译器静态变量
            const clang::APValue& structVal = arg0.getAsStructuralValue();
            if (!structVal.isStruct() || structVal.getStructNumFields() < 3) return size;

            size.x = static_cast<uint32_t>(structVal.getStructField(0).getInt().getExtValue());
            size.y = static_cast<uint32_t>(structVal.getStructField(1).getInt().getExtValue());
            size.z = static_cast<uint32_t>(structVal.getStructField(2).getInt().getExtValue());
            return size;
        }
    private:
        std::vector<Ccino::ShaderIR> shaderIRs;
    };
}

namespace Ccino
{
    // 外部主入口：运行一次 ClangTool 解析该文件，在 MatchCallback 回调里直接把匹配到的所有 Lambda 转成 ShaderIR 返回
    std::vector<ShaderIR> CppParser::ParseFilePassShaders(const std::string& compileCommandJsonPath) {
                

        return {};
    }

    // 内部 AST 处理函数（在 MatchFinder::MatchCallback::run 内部调用，此时 AST 存活）：
    ShaderIR CppParser::ProcessLambdaExpr(const clang::LambdaExpr* lambda, ShaderType shaderType, clang::ASTContext& context) {
        return{};
    }

    ShaderIR CppParser::ProcessComputeShader(const clang::LambdaExpr* lambda, clang::ASTContext& context, const std::string& passName, ThreadGroupSize threadGroupSize) {
        ShaderIR shaderIR;
        shaderIR.threadGroupSize = threadGroupSize;
        shaderIR.shaderType = ShaderType::Compute;
        shaderIR.descriptorSlotLayout = GetDescriptorSlotLayout(lambda, context);
        shaderIR.passName = passName;
        shaderIR.uniformDataBindings = GetUniformDataBindings(lambda, context);
        shaderIR.glsl += "#version 460\n"
                "#extension GL_EXT_buffer_reference2 : require\n";
        std::string localSizeArgs;
        auto appendLocalSize = [&](const char* name, uint32_t size) {
            if (!size) return;
            if (!localSizeArgs.empty()) localSizeArgs += ", ";
            localSizeArgs += std::string(name) + " = " + std::to_string(size);
        };
        appendLocalSize("local_size_x", threadGroupSize.x);
        appendLocalSize("local_size_y", threadGroupSize.y);
        appendLocalSize("local_size_z", threadGroupSize.z);
        shaderIR.glsl += "layout(" + localSizeArgs + ") in;\n";
        
        shaderIR.glsl += GenerateSSBOBinding(lambda, context);
        shaderIR.glsl += GenerateUniformDataReference(lambda, context);
        shaderIR.glsl += GeneratePushConstant();

        //处理lambda函数体
        std::string bodyGlsl = ProcessSTMT(lambda->getBody());

        //处理名字：将第一个参数名替换为 gl_GlobalInvocationID，并将捕获变量替换为 pc.uniformData.<name>
        std::unordered_map<std::string, std::string> mp;
        if (auto* callOp = lambda->getCallOperator()) {
            if (callOp->getNumParams() > 0) {
                std::string paramName = callOp->getParamDecl(0)->getNameAsString();
                if (!paramName.empty()) {
                    mp[paramName] = "gl_GlobalInvocationID";
                }
            }
        }
        for (const auto& binding : shaderIR.uniformDataBindings) {
            if (!binding.name.empty()) {
                mp[binding.name] = "pc.uniformData." + binding.name;
            }
        }
        ChangeBuildInName(bodyGlsl, mp);

        shaderIR.glsl += "void main() { \n";
        shaderIR.glsl += bodyGlsl;
        shaderIR.glsl += "}\n";

        return shaderIR;
    }

    std::string CppParser::ProcessSTMT(clang::Stmt* stmt) {
        if (!stmt) return {};

        if (auto* compoundStmt = llvm::dyn_cast<clang::CompoundStmt>(stmt)) {
            std::string result;
            for (clang::Stmt* subStmt : compoundStmt->body()) {
                result += ProcessSTMT(subStmt);
            }
            return result;
        }
        if (auto* ifStmt = llvm::dyn_cast<clang::IfStmt>(stmt)) {
            return ChangeCppIfToGLSLIf(ifStmt);
        }
        if (auto* forStmt = llvm::dyn_cast<clang::ForStmt>(stmt)) {
            return ChangeCppForToGLSLFor(forStmt);
        }
        if (auto* whileStmt = llvm::dyn_cast<clang::WhileStmt>(stmt)) {
            return ChangeCppWhileToGLSLWhile(whileStmt);
        }
        if (auto* declStmt = llvm::dyn_cast<clang::DeclStmt>(stmt)) {
            return ChangeCppDeclToGLSLDecl(declStmt);
        }
        if (auto* returnStmt = llvm::dyn_cast<clang::ReturnStmt>(stmt)) {
            return ChangeCppReturnToGLSLReturn(returnStmt);
        }
        if (llvm::isa<clang::BreakStmt>(stmt)) {
            return "break;\n";
        }
        if (llvm::isa<clang::ContinueStmt>(stmt)) {
            return "continue;\n";
        }
        if (llvm::isa<clang::NullStmt>(stmt)) {
            return ";\n";
        }
        if (auto* expr = llvm::dyn_cast<clang::Expr>(stmt)) {
            std::string exprStr = ChangeCppExprToGLSLExpr(expr);
            return exprStr.empty() ? std::string {} : (exprStr + ";\n");
        }

        llvm::errs() << "Error: Unsupported Stmt: " << stmt->getStmtClassName() << "\n";
        return {};
    }

    // AST 转换工具函数
    std::string CppParser::ChangeCppTypeToGLSLType(clang::QualType type) {
        if (type.isNull()) return "";
        // 剥离引用(&)、const/volatile，并拿到去糖后的底层真实类型 (穿透 using Vec3 / uint32_t / auto)
        clang::QualType canonical = type.getNonReferenceType().getCanonicalType().getUnqualifiedType();
        // 处理基础内置类型 (int, uint32_t, float, double, bool, void)
        if (const auto* builtin = canonical->getAs<clang::BuiltinType>()) {
            switch (builtin->getKind()) {
                case clang::BuiltinType::Void: return "void";
                case clang::BuiltinType::Bool: return "bool";
                case clang::BuiltinType::Int: return "int";
                case clang::BuiltinType::UInt: return "uint";  // uint32_t 和 unsigned int 在这都会命中
                case clang::BuiltinType::Float: return "float";
                case clang::BuiltinType::Double: return "double";
                default: break;
            }
        }
        // 处理结构体与模板特化 (CcinoType.h 中的 Vec / Mat / Array 以及用户自定义 struct)
        if (const clang::CXXRecordDecl* record = canonical->getAsCXXRecordDecl()) {
            if (const auto* spec = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record)) {
                llvm::StringRef tmplName = spec->getName();
                const clang::TemplateArgumentList& args =
                    spec->getTemplateArgs();  // 这里直接得到模板名并且没有命名空间
                // 匹配 glm::vec<L, T, Q> -> 自动合成 vec3 / dvec3 / ivec3 / uvec3 / bvec3
                if (tmplName == "vec" && args.size() >= 2) {
                    int64_t len = args[0].getAsIntegral().getExtValue();  // 2, 3, 4
                    clang::QualType elemType = args[1].getAsType();
                    std::string prefix;
                    if (elemType->isBooleanType()) prefix = "b";  // 确认模板参数
                    else if (elemType->isSignedIntegerType()) prefix = "i";
                    else if (elemType->isUnsignedIntegerType()) prefix = "u";
                    else if (elemType->isSpecificBuiltinType(clang::BuiltinType::Double)) prefix = "d";
                    // float 前缀为空
                    return prefix + "vec" + std::to_string(len);
                }
                // 匹配 glm::mat<C, R, T, Q> -> 自动合成 mat4 / mat3x4 / dmat4 等
                if (tmplName == "mat" && args.size() >= 3) {
                    int64_t cols = args[0].getAsIntegral().getExtValue();  // 转换为int64_t
                    int64_t rows = args[1].getAsIntegral().getExtValue();
                    clang::QualType elemType = args[2].getAsType();
                    std::string prefix = elemType->isSpecificBuiltinType(clang::BuiltinType::Double) ? "d" : "";
                    if (cols == rows) {
                        return prefix + "mat" + std::to_string(cols);  // 如 mat4, dmat3
                    }
                    return prefix + "mat" + std::to_string(cols) + "x" + std::to_string(rows);  // 如 mat3x4
                }
                // 匹配 Ccino::Array<T, N> (底层为 std::array<T, N>) -> 合成 T[N]
                if (tmplName == "array" && args.size() >= 2) {
                    std::string elemGLSL = ChangeCppTypeToGLSLType(args[0].getAsType());  // 递归处理内部类型
                    int64_t count = args[1].getAsIntegral().getExtValue();
                    return elemGLSL + "[" + std::to_string(count) + "]";
                }

                // 匹配Accessor<T, Flags> ->提取内部Buffer类型
                if (tmplName == "Accessor" && args.size() == 2) {
                    return ChangeCppTypeToGLSLType(args[0].getAsType());
                }
            }

            // 普通用户自定义结构体（如 struct Particle），直接返回结构体名
            return record->getNameAsString();
        }
        return canonical.getAsString();
    }

    // 单独处理Accessor 生成layout(buffer) binding 信息
    std::string CppParser::ChangeAccessorToSSBOReference(const clang::ValueDecl* value) {
        if (!value) return {};
        clang::QualType type = value->getType();
        auto canonical = type.getNonReferenceType().getCanonicalType().getUnqualifiedType();  // 去掉引用 去掉别名
                                                                                              // 去掉cv
        if (const clang::CXXRecordDecl* record = canonical->getAsCXXRecordDecl()) {
            if (const auto* spec = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record)) {
                llvm::StringRef tmplName = spec->getName();
                const clang::TemplateArgumentList& args =
                    spec->getTemplateArgs();  // 这里直接得到模板名并且没有命名空间
                if (tmplName == "Accessor" && args.size() >= 2) {
                    if (args[1].getKind() == clang::TemplateArgument::Declaration) {
                        if (llvm::isa<clang::TemplateParamObjectDecl>(args[1].getAsDecl())) {
                            return "layout(buffer_reference, std430) buffer " +
                                   GetGLSLDefine(args[0].getAsType(), false);
                        }
                    }
                    else {
                        llvm::errs() << "Error: Accessor template need BufferAccessFlags as second argument: "
                                     << tmplName << "\n";
                        return {};
                    }
                }
            }
        }
        return {};
    }


    std::string CppParser::ChangeCppExprToGLSLExpr(clang::Expr* expr) {
        if (!expr) return {};
        expr = expr->IgnoreUnlessSpelledInSource();  // 剔除重复的代码
        if (auto* DRE = llvm::dyn_cast<clang::DeclRefExpr>(expr)) {
            // 只有一个值时走,引用表达式
            return DRE->getDecl()->getNameAsString();
        }

        if (auto* ME = llvm::dyn_cast<clang::MemberExpr>(expr)) {
            if (ME->isImplicitAccess()) {
                return ME->getMemberDecl()->getNameAsString();
            }
            std::string base = ChangeCppExprToGLSLExpr(ME->getBase());
            std::string member = ME->getMemberDecl()->getNameAsString();
            if (base.empty() || member.empty()) return {};
            return base + "." + member;
        }

        if (auto* ASE = llvm::dyn_cast<clang::ArraySubscriptExpr>(expr)) {
            return ChangeCppExprToGLSLExpr(ASE->getBase()) + "[" +
                   ChangeCppExprToGLSLExpr(ASE->getIdx()) + "]";
        }

        if (auto* Op = llvm::dyn_cast<clang::CXXOperatorCallExpr>(expr)) {
            std::string op = clang::getOperatorSpelling(Op->getOperator());

            if (Op->getOperator() == clang::OO_Subscript && Op->getNumArgs() == 2) {
                return ChangeCppExprToGLSLExpr(Op->getArg(0)) + "[" +
                       ChangeCppExprToGLSLExpr(Op->getArg(1)) + "]";
            }

            if (Op->getOperator() == clang::OO_PlusPlus || Op->getOperator() == clang::OO_MinusMinus) {
                const clang::Expr* operand = Op->getArg(0);
                std::string operandGLSL = ChangeCppExprToGLSLExpr(const_cast<clang::Expr*>(operand));

                return Op->getNumArgs() == 2  // 如果为2 则为后缀 如果为1 则为前缀
                           ? "(" + operandGLSL + op + ")"
                           : "(" + op + operandGLSL + ")";
            }

            if (Op->isInfixBinaryOp()) {
                // 中缀
                return "(" + ChangeCppExprToGLSLExpr(Op->getArg(0)) + " " + op + " " +
                       ChangeCppExprToGLSLExpr(Op->getArg(1)) + ")";
            }

            if (Op->getNumArgs() == 1) {
                // 前缀
                //  -a、!a、~a 等前缀一元运算符
                return "(" + op + ChangeCppExprToGLSLExpr(Op->getArg(0)) + ")";
            }
        }

        if (auto* Op = llvm::dyn_cast<clang::BinaryOperator>(expr)) {
            std::string op = Op->getOpcodeStr().str();
            return "(" + ChangeCppExprToGLSLExpr(Op->getLHS()) + " " + op + " " +
                   ChangeCppExprToGLSLExpr(Op->getRHS()) + ")";
        }

        // 处理没用重载的表达式
        if (auto* UO = llvm::dyn_cast<clang::UnaryOperator>(expr)) {
            std::string op = clang::UnaryOperator::getOpcodeStr(UO->getOpcode()).str();
            std::string sub = ChangeCppExprToGLSLExpr(UO->getSubExpr());
            return UO->isPostfix() ? "(" + sub + op + ")" : "(" + op + sub + ")";
        }

        // 浮点数字面量
        if (auto* FL = llvm::dyn_cast<clang::FloatingLiteral>(expr)) {
            llvm::SmallString<16> S;
            FL->getValue().toString(S);
            std::string str = S.str().str();
            if (str.find('.') == std::string::npos && str.find('e') == std::string::npos &&
                str.find('E') == std::string::npos) {
                str += ".0";
            }
            return str;
        }

        if (auto* IL = llvm::dyn_cast<clang::IntegerLiteral>(expr)) {
            llvm::SmallString<32> S;
            IL->getValue().toString(S, 10, IL->getType()->isSignedIntegerType());
            return S.str().str();
        }

        if (auto* BL = llvm::dyn_cast<clang::CXXBoolLiteralExpr>(expr)) {
            return BL->getValue() ? "true" : "false";
        }

        // 构造函数
        if (auto* Cstr = llvm::dyn_cast<clang::CXXConstructExpr>(expr)) {
            // 调用构造函数
            std::string R = ChangeCppTypeToGLSLType(Cstr->getType()) + "(";
            for (uint32_t i = 0; i < Cstr->getNumArgs(); ++i) {
                R += (i ? "," : "") + ChangeCppExprToGLSLExpr(Cstr->getArg(i));
            }
            return R + ")";
        }

        // 函数调用侧
        if (auto* Call = llvm::dyn_cast<clang::CallExpr>(expr)) {  // glm::dot(a,b) / Ccino::Dot(a,b) -> dot(a,b)
            if (auto* FD = Call->getDirectCallee()) {
                std::string funcName = FD->getName().str();
                std::string lowerName = funcName;
                std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                if (GLSLBuiltinFuncSet.contains(lowerName)) {
                    funcName = std::move(lowerName);
                }
                std::string R = funcName + "(";
                for (unsigned i = 0; i < Call->getNumArgs(); ++i)
                    R += (i ? ", " : "") + ChangeCppExprToGLSLExpr(Call->getArg(i));
                return R + ")";
            }
        }

        llvm::errs() << "Error: Accessor template need BufferAccessFlags as second argument: " +
                            std::string(expr->getStmtClassName()) + "\n";
        return {};  // 直接返回空
    }

    std::string CppParser::ChangeCppExprListToGLSLExprList(llvm::ArrayRef<const clang::Expr*> exprs) {
        std::string result;
        for (size_t i = 0; i < exprs.size(); ++i) {
            if (!exprs[i]) return {};
            const std::string converted = ChangeCppExprToGLSLExpr(const_cast<clang::Expr*>(exprs[i]));
            if (converted.empty()) return {};
            if (i != 0) result += ", ";
            result += converted;
        }
        return result;
    }

    std::string CppParser::ChangeCppReturnToGLSLReturn(clang::ReturnStmt* returnStmt) {
        std::string rtStr = "return ";
        if (auto* expr = returnStmt->getRetValue()) {
            rtStr += ChangeCppExprToGLSLExpr(expr);
        }
        return rtStr + ";\n";
    }

    std::string CppParser::ChangeCppIfToGLSLIf(clang::IfStmt* ifStmt) {
        std::string rtStr = "if (";
        if (auto* initStmt = ifStmt->getInit()) {
            llvm::errs() << "GLSL does not support if init-statement\n";
            return {};
        }
        if (auto* condExpr = ifStmt->getCond()) {
            rtStr += ChangeCppExprToGLSLExpr(condExpr);
            rtStr += ") \n";
        }
        auto* thenStmt = ifStmt->getThen();
        auto* elseStmt = ifStmt->getElse();


        if (thenStmt) {
            rtStr += "{ \n";
            rtStr += ProcessSTMT(thenStmt);
            rtStr += "}\n";
        }
        else {
            llvm::errs() << "GLSL does not support Non if then-statement\n";
        }

        if (elseStmt) {
            rtStr += "else {\n";
            rtStr += ProcessSTMT(elseStmt);
            rtStr += "}\n";
        }

        return rtStr;
    }

    std::string CppParser::ChangeCppForToGLSLFor(clang::ForStmt* forStmt) {
        if (auto* range = llvm::dyn_cast<clang::CXXForRangeStmt>(forStmt)) {
            llvm::errs() << "GLSL does not support range-for declaration\n";
            return {};
        }

        if (auto* declStmt = forStmt->getConditionVariableDeclStmt()) {
            llvm::errs() << "GLSL does not support condition-variable declaration\n";
            return {};
        }


        std::string rtStr = "for (";
        if (auto* initStmt = forStmt->getInit()) {
            rtStr += ProcessSTMT(initStmt);
        }
        else {
            rtStr += ";";
        }

        if (auto* condExpr = forStmt->getCond()) {
            rtStr += ChangeCppExprToGLSLExpr(condExpr);
        }
        rtStr += ";";

        if (auto* incStmt = forStmt->getInc()) {
            rtStr += ChangeCppExprToGLSLExpr(incStmt);
        }


        rtStr += ") { \n";
        rtStr += ProcessSTMT(forStmt->getBody());
        rtStr += "}";


        return rtStr;
    }

    std::string CppParser::ChangeCppWhileToGLSLWhile(clang::WhileStmt* whileStmt) {
        // 初版本简单点不支持复杂的语法
        if (whileStmt->getConditionVariable()) {
            // 例如 while (int x = f())，第一版可不支持
            llvm::errs() << "GLSL does not support condition-variable declaration in While STMT\n";
            return {};
        }

        const std::string cond = ChangeCppExprToGLSLExpr(const_cast<clang::Expr*>(whileStmt->getCond()));

        const std::string body = ProcessSTMT(const_cast<clang::Stmt*>(whileStmt->getBody()));

        if (cond.empty()) {
            return {};
        }

        return "while (" + cond + ") {\n" + body + "}";
    }

    std::string CppParser::ChangeCppInitToGLSLInitializer(const clang::VarDecl* var) {
        if (!var) return {};
        const clang::Expr* init = var->getInit();
        if (!init) return {};

        if (const auto* list = llvm::dyn_cast<clang::InitListExpr>(init)) {
            if (const auto* semantic = list->getSemanticForm()) list = semantic;
            std::vector<const clang::Expr*> elements;
            elements.reserve(list->getNumInits());
            for (const clang::Expr* element : list->inits()) {
                if (element) elements.push_back(element);
            }
            if (var->getType()->isRecordType()) { //struct class enum 类型
                const std::string type = ChangeCppTypeToGLSLType(var->getType());
                if (type.empty()) return {};
                return type + "(" + ChangeCppExprListToGLSLExprList(elements) + ")";
            }
            return "{" + ChangeCppExprListToGLSLExprList(elements) + "}";
        }

        if (const auto* construct = llvm::dyn_cast<clang::CXXConstructExpr>(init)) {
            const std::string type = ChangeCppTypeToGLSLType(var->getType());
            if (type.empty()) return {};
            std::vector<const clang::Expr*> args;
            args.reserve(construct->getNumArgs());
            for (const clang::Expr* arg : construct->arguments()) {
                if (arg) args.push_back(arg);
            }
            return type + "(" + ChangeCppExprListToGLSLExprList(args) + ")";
        }

        return ChangeCppExprToGLSLExpr(const_cast<clang::Expr*>(init));
    }

    std::string CppParser::ChangeCppDeclToGLSLDecl(clang::DeclStmt* declStmt) {
        if (!declStmt) return {};

        // GLSL 的一次声明只能有一个类型，例如 C++ 的 "int a = 1, b = 2"
        // 要拆成 "int a = 1;\nint b = 2;\n"。
        std::string rtStr;
        for (const clang::Decl* decl : declStmt->decls()) {
            const auto* var = llvm::dyn_cast<clang::VarDecl>(decl);
            if (!var) {
                llvm::errs() << "Error: unsupported declaration in DeclStmt: " << decl->getDeclKindName() << "\n";
                return {};
            }

            const std::string name = var->getNameAsString();
            if (name.empty()) return {};

            clang::QualType canonical = var->getType().getNonReferenceType().getCanonicalType().getUnqualifiedType();
            if (const clang::CXXRecordDecl* record = canonical->getAsCXXRecordDecl()) {
                if (const auto* spec = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record)) {
                    if (spec->getName() == "Accessor") {
                        llvm::errs() << "Error: Accessor cannot be declared inside shader function body: " << name
                                     << "\n";
                        return {};
                    }
                }
            }

            // getType() 对 auto 返回已经推导完成的类型，而不是源码里的 auto。
            const std::string type = ChangeCppTypeToGLSLType(var->getType());
            if (type.empty()) return {};

            // GLSL 的数组维度必须写在变量名后：float values[4]。
            const size_t bracket = type.find('[');
            const std::string typeName = type.substr(0, bracket);
            const std::string dimensions = bracket == std::string::npos ? std::string() : type.substr(bracket);
            if (typeName.empty()) return {};

            // GLSL 没有 C++ 的 static/extern 局部变量语义；着色器里按普通变量处理。
            rtStr += typeName + " " + name + dimensions;
            const std::string initializer = ChangeCppInitToGLSLInitializer(var);
            if (!initializer.empty()) {
                rtStr += " = " + initializer;
            }
            rtStr += ";\n";
        }
        return rtStr;
    }

    void CppParser::ChangeBuildInName(std::string& code, const std::unordered_map<std::string, std::string>& buildInMap) {
        if (code.empty() || buildInMap.empty()) return;

        static const std::regex identRegex(R"(\b[A-Za-z_]\w*\b)");

        std::string result;
        result.reserve(code.size());

        std::sregex_iterator it(code.begin(), code.end(), identRegex);
        std::sregex_iterator end;
        std::size_t lastPos = 0;

        for (; it != end; ++it) {
            const std::smatch& match = *it;
            result.append(code, lastPos, match.position() - lastPos);

            const bool isMemberAccess =
                (match.position() > 0 && code[static_cast<std::size_t>(match.position() - 1)] == '.');
            auto found = isMemberAccess ? buildInMap.end() : buildInMap.find(match.str());
            if (found != buildInMap.end()) {
                result.append(found->second);
            }
            else {
                result.append(match.str());
            }

            lastPos = match.position() + match.length();
        }

        result.append(code, lastPos, std::string::npos);
        code = std::move(result);
    }


    std::string CppParser::GenerateGlobalFunctionToGLSL(clang::FunctionDecl* functionDecl) {
        if (!functionDecl) return {};
        auto* definition = functionDecl->getDefinition();
        if (!definition) {
            llvm::errs() << "Error: No definition found\n";
            return {};
        }

        if (!definition->hasBody()) {
            llvm::errs() << "Error: No body found\n";
            return {};
        }
        clang::Stmt* body = definition->getBody();

        std::string result = ChangeCppTypeToGLSLType(definition->getReturnType()) + " ";
        result += definition->getNameAsString() + "(";

        for (unsigned int i = 0; i < definition->getNumParams(); ++i) {
            result += i ? ", " : "";
            auto* paramDecl = definition->getParamDecl(i);
            result += ChangeCppTypeToGLSLType(paramDecl->getType()) + " " + paramDecl->getNameAsString();
        }
        result += ") {\n";
        result += ProcessSTMT(body);
        result += "}\n";
        return result;
    }

    std::string CppParser::GenerateSSBOBinding(const clang::LambdaExpr* lambdaExpr, clang::ASTContext& context) {
        if (!lambdaExpr) return {};

        const clang::CXXRecordDecl* lambdaClass = lambdaExpr->getLambdaClass();
        if (!lambdaClass) return {};

        std::string result;
        std::unordered_set<std::string> emittedSSBOs;

        for (const auto* field : lambdaClass->fields()) {
            std::string ssbo = ChangeAccessorToSSBOReference(field);
            if (!ssbo.empty() && emittedSSBOs.insert(ssbo).second) {
                result += ssbo;
            }
        }

        return result;
    }



    std::string CppParser::GenerateUniformDataReference(const clang::LambdaExpr* lambdaExpr, clang::ASTContext& context) {
        std::string result =
            "layout(buffer_reference, std140, buffer_reference_align = 16) readonly buffer UniformData { \n";

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
            std::string varType = ChangeCppTypeToGLSLType(field->getType());
            result += varType + " " + varName + "; \n";
            fieldIndex++;
        }

        return result + "}; \n";
    }

    std::string CppParser::GeneratePushConstant() {
        return "layout(push_constant) uniform PC {\n"
               "    UniformData uniformData;\n"
               "} pc;\n";
    }

    std::string CppParser::ChangeCppOperatorToGLSLOperator(clang::BinaryOperatorKind op) {
        // 仅仅支持简单的操作
        return clang::BinaryOperator::getOpcodeStr(op).str();  // 这里把OP_Plus -> + 这种形式
    }

    std::string CppParser::ChangeCppOperatorToGLSLOperator(clang::OverloadedOperatorKind op) {
        if (auto spelling = clang::getOperatorSpelling(op)) {
            return std::string(spelling);
        }
        return {};  // 空则不支持此操作
    }

    std::string CppParser::GetGLSLDefine(clang::QualType type, bool needStructHeader) {
        clang::QualType canonical = type.getNonReferenceType().getCanonicalType().getUnqualifiedType();
        std::string glslDefine = "";
        if (const clang::CXXRecordDecl* record = canonical->getAsCXXRecordDecl()) {
            if (const auto def = record->getDefinition()) {
                // 直接得到完整类型，防止隐式类型
                if (needStructHeader) {
                    glslDefine += "struct " + def->getNameAsString() + " {\n";
                }
                else {
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
                case clang::BuiltinType::Bool: return UniformDataType::Bool;
                case clang::BuiltinType::Int: return UniformDataType::Int;
                case clang::BuiltinType::UInt: return UniformDataType::UInt;
                case clang::BuiltinType::ULong:
                case clang::BuiltinType::ULongLong: return UniformDataType::Buffer;  // uint64_t (BDA)
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

    std::vector<ShaderUniformDataBinding> CppParser::GetUniformDataBindings(const clang::LambdaExpr* lambdaExpr,
                                                                            clang::ASTContext& context) {
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


    bool CppParser::IsGlobalVariable(clang::ValueDecl* value) {
        if (const auto* varDecl = llvm::dyn_cast<clang::VarDecl>(value)) {
            // 值声明变为变量声明
            //  1. 判断是否在文件/命名空间作用域声明（真正的全局变量 + 类的 static 静态成员变量）
            bool isGlobalScopeVar = varDecl->isFileVarDecl();

            // 2. 判断是否具有全局存储期（包含上面的全局变量 + 函数内部的 static 局部变量）
            bool hasGlobalStorage = varDecl->hasGlobalStorage();

            // 3. 判断是否为函数内的普通局部变量（不含函数参数）
            bool isLocalVar = varDecl->isLocalVarDecl();

            return isGlobalScopeVar && hasGlobalStorage && !isLocalVar;
        }
        return false;
    }

    DescriptorSlotLayout CppParser::GetDescriptorSlotLayout(const clang::LambdaExpr* lambdaExpr, clang::ASTContext& context) {
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
}  // namespace Ccino