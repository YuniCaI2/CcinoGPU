#include "CodeGenerator/ASTGenerator.h"
#include "CodeGenerator/SignatureFlattener.h"
#include "Core/CcinoType.h"
#include "GLSLAST.h"
#include "SignatureFlattener.h"
#include <algorithm>
#include <cctype>
#include <clang/AST/ASTContext.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/OperationKinds.h>
#include <clang/AST/Type.h>
#include <clang/Basic/OperatorKinds.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/raw_ostream.h>
#include <memory>
#include <string>
#include <utility>

namespace Ccino {
    std::unique_ptr<GLSL::LoweringStmt> ASTGenerator::ProcessClangExpr(clang::Expr* expr) {
        if (!expr) return {};
        expr = expr->IgnoreUnlessSpelledInSource();

        auto lowering = std::make_unique<GLSL::LoweringStmt>();

        // 禁止在 Shader 函数体内嵌套局部 Lambda
        if (llvm::isa<clang::LambdaExpr>(expr)) {
            llvm::errs() << "Error: Unsupported nested LambdaExpr in ASTGenerator\n";
            return {};
        }

        // 默认参数表达式
        if (auto* DAE = llvm::dyn_cast<clang::CXXDefaultArgExpr>(expr)) {
            return ProcessClangExpr(DAE->getExpr());
        }

        // 显式 this 指针 (如 this->member)
        if (llvm::isa<clang::CXXThisExpr>(expr)) {
            auto thisExpr = std::make_unique<GLSL::Expr>();
            thisExpr->text = "self";
            lowering->expr = std::move(thisExpr);
            return lowering;
        }

        if (auto* DRE = llvm::dyn_cast<clang::DeclRefExpr>(expr)) {
            // 只有一个值时走,引用表达式
            auto refExpr = std::make_unique<GLSL::Expr>();
            auto* decl = DRE->getDecl();
            auto name = decl->getNameAsString();
            const auto* varDecl = llvm::dyn_cast<clang::VarDecl>(decl);

            if (DRE->refersToEnclosingVariableOrCapture() || (varDecl && varDecl->isInitCapture())) { //是被捕获的变量
                // Lambda 捕获的外部变量 (Accessor / Uniform 标量 / 向量 / 矩阵 / 广义初始化捕获)
                refExpr->text = "pc.uniformData." + name;
            } else if (const auto* enumConstant = llvm::dyn_cast<clang::EnumConstantDecl>(decl)) {
                llvm::SmallString<32> S;
                enumConstant->getInitVal().toString(S, 10);
                refExpr->text = S.str().str();
            } else if ((!varDecl || !varDecl->isLocalVarDeclOrParm()) && GLSLBuiltinVarMap.contains(name)) {
                // C++ 内建全局变量 (如 Ccino::ComputeGlobalID -> gl_GlobalInvocationID)
                refExpr->text = GLSLBuiltinVarMap.at(name); //全局变量羊二十DeclRefExpr 所以这里直接查表替换即可
            } else {
                refExpr->text = name;
            }

            lowering->expr = std::move(refExpr);
            return lowering;
        }

        //成员访问
        if (auto* ME = llvm::dyn_cast<clang::MemberExpr>(expr)) {
            return ProcessMemberExpr(ME);
        }

        //ASE
        if (auto* ASE = llvm::dyn_cast<clang::ArraySubscriptExpr>(expr)) {
            auto base = ProcessClangExpr(ASE->getBase());
            auto idx = ProcessClangExpr(ASE->getIdx());
            if (!base || !base->expr || !idx || !idx->expr) return {};
            for (auto& idxField : idx->fields) {
                base->fields.push_back(std::move(idxField));
            }
            base->expr->text += "[" + idx->expr->text + "]";
            return base;
        }

        //Member Call (必须在 CallExpr 之前)
        if (auto* MCE = llvm::dyn_cast<clang::CXXMemberCallExpr>(expr)) {
            return ProcessMemberCallExpr(MCE);
        }

        //重载运算符调用 (必须在 CallExpr 之前，因为 CXXOperatorCallExpr 继承自 CallExpr)
        if (auto* Op = llvm::dyn_cast<clang::CXXOperatorCallExpr>(expr)) {
            return ProcessOperatorCallExpr(Op);
        }

        if (auto* Call = llvm::dyn_cast<clang::CallExpr>(expr)) {
            return ProcessCallExpr(Call);
        }

        //内置二元运算符
        if (auto* Op = llvm::dyn_cast<clang::BinaryOperator>(expr)) {
            auto lhs = ProcessClangExpr(Op->getLHS());
            auto rhs = ProcessClangExpr(Op->getRHS());
            if (!lhs || !lhs->expr || !rhs || !rhs->expr) return {};
            for (auto& f : lhs->fields) {
                lowering->fields.push_back(std::move(f));
            }
            for (auto& f : rhs->fields) {
                lowering->fields.push_back(std::move(f));
            }
            std::string op = Op->getOpcodeStr().str();
            auto binExpr = std::make_unique<GLSL::Expr>();
            binExpr->text = "(" + lhs->expr->text + " " + op + " " + rhs->expr->text + ")";
            lowering->expr = std::move(binExpr);
            return lowering;
        }

        //内置一元运算符
        if (auto* UO = llvm::dyn_cast<clang::UnaryOperator>(expr)) {
            auto sub = ProcessClangExpr(UO->getSubExpr());
            if (!sub || !sub->expr) return {};
            for (auto& f : sub->fields) {
                lowering->fields.push_back(std::move(f));
            }
            std::string op = clang::UnaryOperator::getOpcodeStr(UO->getOpcode()).str();
            auto unaryExpr = std::make_unique<GLSL::Expr>();
            unaryExpr->text = UO->isPostfix()
                                  ? "(" + sub->expr->text + op + ")"
                                  : "(" + op + sub->expr->text + ")";
            lowering->expr = std::move(unaryExpr);
            return lowering;
        }

        //三元条件运算符
        if (auto* CO = llvm::dyn_cast<clang::ConditionalOperator>(expr)) {
            auto cond = ProcessClangExpr(CO->getCond());
            auto trueExpr = ProcessClangExpr(CO->getTrueExpr());
            auto falseExpr = ProcessClangExpr(CO->getFalseExpr());
            if (!cond || !cond->expr || !trueExpr || !trueExpr->expr || !falseExpr || !falseExpr->expr) {
                return {};
            }
            for (auto& f : cond->fields) lowering->fields.push_back(std::move(f));
            for (auto& f : trueExpr->fields) lowering->fields.push_back(std::move(f));
            for (auto& f : falseExpr->fields) lowering->fields.push_back(std::move(f));
            auto condOpExpr = std::make_unique<GLSL::Expr>();
            condOpExpr->text = "(" + cond->expr->text + " ? " + trueExpr->expr->text + " : " + falseExpr->expr->text + ")";
            lowering->expr = std::move(condOpExpr);
            return lowering;
        }

        //浮点数字面量
        if (auto* FL = llvm::dyn_cast<clang::FloatingLiteral>(expr)) {
            return ProcessFloatLiteral(FL);
        }

        //整数字面量
        if (auto* IL = llvm::dyn_cast<clang::IntegerLiteral>(expr)) {
            llvm::SmallString<32> S;
            IL->getValue().toString(S, 10, IL->getType()->isSignedIntegerType());
            auto litExpr = std::make_unique<GLSL::Expr>();
            litExpr->text = S.str().str();
            lowering->expr = std::move(litExpr);
            return lowering;
        }

        //布尔字面量
        if (auto* BL = llvm::dyn_cast<clang::CXXBoolLiteralExpr>(expr)) {
            return ProcessBinaryLiteral(BL);
        }

        //显式类型转换 (如 float(x), (float)x, static_cast<float>(x))
        if (auto* Cast = llvm::dyn_cast<clang::ExplicitCastExpr>(expr)) {
            return ProcessExplicitCast(Cast);
        }

        //构造函数
        if (auto* Cstr = llvm::dyn_cast<clang::CXXConstructExpr>(expr)) {
            return ProcessConstruct(Cstr);
        }

        //初始化列表表达式 (如 {1, 2, 3} 或聚合初始化 Particle{pos, 1})
        if (auto* ILE = llvm::dyn_cast<clang::InitListExpr>(expr)) {
            return ProcessInitList(ILE);
        }

        llvm::errs() << "Error: Unsupported Expr in ASTGenerator: " << expr->getStmtClassName() << "\n";
        return {};
    }

    std::unique_ptr<GLSL::LoweringStmt> ASTGenerator::ProcessOperatorCallExpr(clang::CXXOperatorCallExpr* expr) {
        if (!expr) return {};

        auto lowering = std::make_unique<GLSL::LoweringStmt>();
        std::string op = clang::getOperatorSpelling(expr->getOperator());

        if (expr->getOperator() == clang::OO_Subscript && expr->getNumArgs() == 2) {
            auto base = ProcessClangExpr(expr->getArg(0));
            auto idx = ProcessClangExpr(expr->getArg(1));
            if (!base || !base->expr || !idx || !idx->expr) return {};
            for (auto& f : idx->fields) {
                base->fields.push_back(std::move(f));
            }
            base->expr->text += "[" + idx->expr->text + "]";
            return base;
        }

        if ((expr->getOperator() == clang::OO_PlusPlus || expr->getOperator() == clang::OO_MinusMinus) &&
            expr->getNumArgs() >= 1) {
            auto operand = ProcessClangExpr(expr->getArg(0));
            if (!operand || !operand->expr) return {};
            for (auto& f : operand->fields) {
                lowering->fields.push_back(std::move(f));
            }
            auto opExpr = std::make_unique<GLSL::Expr>();
            opExpr->text = (expr->getNumArgs() == 2) //如果参数为2 ，但是为-- 或者 ++ 则为后置， 不然为左置
                               ? "(" + operand->expr->text + op + ")"
                               : "(" + op + operand->expr->text + ")";
            lowering->expr = std::move(opExpr);
            return lowering;
        }

        if (expr->isInfixBinaryOp() && expr->getNumArgs() == 2) {
            auto lhs = ProcessClangExpr(expr->getArg(0));
            auto rhs = ProcessClangExpr(expr->getArg(1));
            if (!lhs || !lhs->expr || !rhs || !rhs->expr) return {};
            for (auto& f : lhs->fields) {
                lowering->fields.push_back(std::move(f));
            }
            for (auto& f : rhs->fields) {
                lowering->fields.push_back(std::move(f));
            }
            auto opExpr = std::make_unique<GLSL::Expr>();
            opExpr->text = "(" + lhs->expr->text + " " + op + " " + rhs->expr->text + ")";
            lowering->expr = std::move(opExpr);
            return lowering;
        }

        if (expr->getNumArgs() == 1) {
            auto sub = ProcessClangExpr(expr->getArg(0));
            if (!sub || !sub->expr) return {};
            for (auto& f : sub->fields) {
                lowering->fields.push_back(std::move(f));
            }
            auto opExpr = std::make_unique<GLSL::Expr>();
            opExpr->text = "(" + op + sub->expr->text + ")";
            lowering->expr = std::move(opExpr);
            return lowering;
        }

        return {};
    }

    std::unique_ptr<GLSL::LoweringStmt> ASTGenerator::ProcessCallExpr(clang::CallExpr* expr) {
        if (!expr) return nullptr;
        if (auto* FD = expr->getDirectCallee()) {
            if (isInGlobalFunctionBody) InsertFunctionDeclToGraph(FD);
            std::string funcName = SignatureFlattener::FlattenFunction(FD);
            std::string rawName = FD->getNameAsString();
            std::string lowerName = rawName;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });

            std::string camelName = rawName;
            if (!camelName.empty()) {
                camelName[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(camelName[0])));
            }

            if (GLSLBuiltinFuncSet.contains(lowerName)) {
                funcName = std::move(lowerName);
            } else if (GLSLBuiltinFuncSet.contains(camelName)) {
                funcName = std::move(camelName);
            }

            auto returnStmt = std::make_unique<GLSL::LoweringStmt>();
            returnStmt->expr = std::make_unique<GLSL::Expr>();
            returnStmt->expr->text = funcName + "(";
            for (unsigned i = 0; i < expr->getNumArgs(); ++i) {
                auto argStmt = ProcessClangExpr(expr->getArg(i));
                if (!argStmt || !argStmt->expr) continue;
                for (auto& field : argStmt->fields) {
                    returnStmt->fields.push_back(std::move(field));
                }
                returnStmt->expr->text += (i ? "," : "") + argStmt->expr->text;
            }

            returnStmt->expr->text += ")";
            return returnStmt;
        }
        return nullptr;
    }

    std::unique_ptr<GLSL::LoweringStmt> ASTGenerator::ProcessMemberCallExpr(clang::CXXMemberCallExpr* expr) {
        if (!expr) return {};

        clang::CXXMethodDecl* method = expr->getMethodDecl();
        if (!method) return {};   // 比如通过成员函数指针 .* / ->* 调用
        if (isInGlobalFunctionBody) InsertFunctionDeclToGraph(method);

        // Viewer 特殊处理
        if (const auto* spec = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(method->getParent())) {
            if (spec->getName() == "Viewer") {
                return ProcessViewerCallExpr(expr);
            }
        }

        auto lowering = std::make_unique<GLSL::LoweringStmt>();

        //对象部分：只有非静态成员函数才需要传入 self
        std::string selfText;
        if (method->isInstance()) {
            clang::Expr* obj = expr->getImplicitObjectArgument(); //得到左边的对象
            clang::Expr* objStripped = obj ? obj->IgnoreParenImpCasts() : nullptr;

            if (!objStripped || llvm::isa<clang::CXXThisExpr>(objStripped)) { //跳过this本身，也就是在类成员函数内部的情况
                // foo() 或 this->foo()
                selfText = "self";
            } else {
                //a.b[i].foo()
                auto objLowering = ProcessClangExpr(obj);
                if (objLowering) {
                    for (auto& f : objLowering->fields)
                        lowering->fields.push_back(std::move(f));
                    if (objLowering->expr) selfText = objLowering->expr->text;
                }
            }
        }

        //函数名：Namespace_ClassName_method_ParamTypes
        std::string funcName = SignatureFlattener::FlattenFunction(method, true);

        //参数
        std::string args = selfText;
        for (clang::Expr* arg : expr->arguments()) {
            auto argLowering = ProcessClangExpr(arg);
            if (!argLowering || !argLowering->expr) continue;
            for (auto& f : argLowering->fields)
                lowering->fields.push_back(std::move(f));
            if (!args.empty()) args += ", ";
            args += argLowering->expr->text;
        }

        auto callExpr = std::make_unique<GLSL::Expr>();
        callExpr->text = funcName + "(" + args + ")";
        lowering->expr = std::move(callExpr);
        return lowering;
    }

    std::unique_ptr<GLSL::LoweringStmt> ASTGenerator::ProcessViewerCallExpr(clang::CXXMemberCallExpr* expr) {
        if (!expr) return {};

        clang::CXXMethodDecl* method = expr->getMethodDecl();
        if (!method) return {};

        const auto* spec = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(method->getParent());
        if (!spec || spec->getName() != "Viewer") return {};

        auto lowering = std::make_unique<GLSL::LoweringStmt>();

        std::string selfText;
        clang::Expr* obj = expr->getImplicitObjectArgument(); //得到左边的类
        if (auto objLowering = ProcessClangExpr(obj)) { //得到图像本身
            for (auto& f : objLowering->fields)
                lowering->fields.push_back(std::move(f)); //融合lowering
            if (objLowering->expr) selfText = objLowering->expr->text;
        }
        if (selfText.empty()) return {};

        const auto& tmplArgs = spec->getTemplateArgs();
        int64_t viewTypeVal = (tmplArgs.size() >= 2 && tmplArgs[1].getKind() == clang::TemplateArgument::Integral)
                                  ? tmplArgs[1].getAsIntegral().getExtValue()
                                  : 0; // 0: Float, 1: Int, 2: UInt

        std::vector<std::string> argTexts;
        for (clang::Expr* arg : expr->arguments()) { //参数
            auto argLowering = ProcessClangExpr(arg); //得到入参
            if (!argLowering || !argLowering->expr) return {};
            for (auto& f : argLowering->fields)
                lowering->fields.push_back(std::move(f));
            argTexts.push_back(std::move(argLowering->expr->text)); //得到第一个参数，也就是coord
        }

        llvm::StringRef methodName = method->getName();
        auto callExpr = std::make_unique<GLSL::Expr>();

        if (methodName == "Load" && argTexts.size() >= 1) {
            const char* imgArr = (viewTypeVal == 1) ? "globalIImages"
                               : (viewTypeVal == 2) ? "globalUImages"
                                                    : "globalImages";
            callExpr->text = std::string("imageLoad(") + imgArr + "[nonuniformEXT(" + selfText + ")], " + argTexts[0] + ")";
            lowering->expr = std::move(callExpr);
            return lowering;
        }

        if (methodName == "Store" && argTexts.size() >= 2) {
            const char* imgArr = (viewTypeVal == 1) ? "globalIImages"
                               : (viewTypeVal == 2) ? "globalUImages"
                                                    : "globalImages";
            callExpr->text = std::string("imageStore(") + imgArr + "[nonuniformEXT(" + selfText + ")], " + argTexts[0] + ", " + argTexts[1] + ")";
            lowering->expr = std::move(callExpr);
            return lowering;
        }

        if (methodName == "Sample" && argTexts.size() >= 3) {
            const char* texArr = (viewTypeVal == 1) ? "globalITextures"
                               : (viewTypeVal == 2) ? "globalUTextures"
                                                    : "globalTextures";
            const char* samplerCtor = (viewTypeVal == 1) ? "isampler2D"
                                    : (viewTypeVal == 2) ? "usampler2D"
                                                         : "sampler2D";
            callExpr->text = std::string("textureLod(") + samplerCtor + "(" + texArr + "[nonuniformEXT(" + selfText + ")], globalSamplers[nonuniformEXT(" + argTexts[1] + ")]), " + argTexts[0] + ", " + argTexts[2] + ")";
            lowering->expr = std::move(callExpr);
            return lowering;
        }

        return {};
    }

    std::unique_ptr<GLSL::LoweringStmt> ASTGenerator::ProcessMemberExpr(clang::MemberExpr* expr) {
        if (!expr) return {};

        auto lowering = std::make_unique<GLSL::LoweringStmt>();

        auto returnExpr = std::make_unique<GLSL::Expr>();
        auto name = expr->getMemberDecl()->getNameAsString();

        if (name.empty()) {
            return ProcessClangExpr(expr->getBase());
        }
        //这个需要处理下如果current指针在，那代表不是lambda
        if (expr->isImplicitAccess()) {
            if (m_currentStructDecl) {
                returnExpr->text = "self." + name;
                lowering->expr = std::move(returnExpr);
            } else {
                //这里是不会走到的，因为不会出现Func嵌套Lambda的情况
                returnExpr->text = "pc.uniformData." + name;
                lowering->expr = std::move(returnExpr);
            }
            return lowering;
        }

        auto baseLowering = ProcessClangExpr(expr->getBase());
        if (!baseLowering || !baseLowering->expr) return {};
        lowering->fields = std::move(baseLowering->fields); //处理展开
        returnExpr->text = baseLowering->expr->text + "." + name;
        lowering->expr = std::move(returnExpr);
        return lowering;
    }

    std::unique_ptr<GLSL::LoweringStmt> ASTGenerator::ProcessILExpr(clang::IntegerLiteral* expr) {
        if (!expr) return {};
        auto result = std::make_unique<GLSL::LoweringStmt>();
        result->expr = std::make_unique<GLSL::Expr>();

        llvm::SmallString<32> S;
        expr->getValue().toString(S, 10, expr->getType()->isSignedIntegerType()); //使用十进制
        result->expr->text = S.str().str();
        return result;
    }

    std::unique_ptr<GLSL::LoweringStmt> ASTGenerator::ProcessBinaryLiteral(clang::CXXBoolLiteralExpr* expr) {
        if (!expr) return {};
        auto result = std::make_unique<GLSL::LoweringStmt>();
        result->expr = std::make_unique<GLSL::Expr>();

        result->expr->text = expr->getValue() ? "true" : "false";
        return result;
    }

    //内置二元运算符
    std::unique_ptr<GLSL::LoweringStmt> ASTGenerator::ProcessBinaryOp(clang::BinaryOperator* expr) {
        if (!expr) return {};
        auto result = std::make_unique<GLSL::LoweringStmt>();
        result->expr = std::make_unique<GLSL::Expr>();

        auto lhs = ProcessClangExpr(expr->getLHS());
        auto rhs = ProcessClangExpr(expr->getRHS());
        if (!lhs || !lhs->expr || !rhs || !rhs->expr) return {};
        for (auto& f : lhs->fields) {
            result->fields.push_back(std::move(f));
        }
        for (auto& f : rhs->fields) {
            result->fields.push_back(std::move(f));
        }
        std::string op = expr->getOpcodeStr().str();
        auto binExpr = std::make_unique<GLSL::Expr>();
        binExpr->text = "(" + lhs->expr->text + " " + op + " " + rhs->expr->text + ")";
        result->expr = std::move(binExpr);
        return result;
    }

    std::unique_ptr<GLSL::LoweringStmt> ASTGenerator::ProcessUnaryOp(clang::UnaryOperator* expr) {
        if (!expr) return {};
        auto result = std::make_unique<GLSL::LoweringStmt>();
        result->expr = std::make_unique<GLSL::Expr>();

        auto sub = ProcessClangExpr(expr->getSubExpr());
        if (!sub || !sub->expr) return {};
        for (auto& f : sub->fields) {
            result->fields.push_back(std::move(f));
        }
        std::string op = clang::UnaryOperator::getOpcodeStr(expr->getOpcode()).str();
        auto unaryExpr = std::make_unique<GLSL::Expr>();
        unaryExpr->text = expr->isPostfix() //是否是后置
                                ? "(" + sub->expr->text + op + ")"
                                : "(" + op + sub->expr->text + ")";
        result->expr = std::move(unaryExpr);
        return result;
    }

    std::unique_ptr<GLSL::LoweringStmt> ASTGenerator::ProcessCondOp(clang::ConditionalOperator* expr) {
        if (!expr) return {};
        auto result = std::make_unique<GLSL::LoweringStmt>();
        result->expr = std::make_unique<GLSL::Expr>();

        auto cond = ProcessClangExpr(expr->getCond());
        auto trueExpr = ProcessClangExpr(expr->getTrueExpr());
        auto falseExpr = ProcessClangExpr(expr->getFalseExpr());
        if (!cond || !cond->expr || !trueExpr || !trueExpr->expr || !falseExpr || !falseExpr->expr) {
            return {};
        }
        for (auto& f : cond->fields) result->fields.push_back(std::move(f));
        for (auto& f : trueExpr->fields) result->fields.push_back(std::move(f));
        for (auto& f : falseExpr->fields) result->fields.push_back(std::move(f));
        auto condOpExpr = std::make_unique<GLSL::Expr>();
        condOpExpr->text = "(" + cond->expr->text + " ? " + trueExpr->expr->text + " : " + falseExpr->expr->text + ")";
        result->expr = std::move(condOpExpr);
        return result;
    }

    std::unique_ptr<GLSL::LoweringStmt> ASTGenerator::ProcessFloatLiteral(clang::FloatingLiteral* expr) {
        if (!expr) return {};
        auto result = std::make_unique<GLSL::LoweringStmt>();
        result->expr = std::make_unique<GLSL::Expr>();

        llvm::SmallString<16> S;
        expr->getValue().toString(S);
        std::string str = S.str().str();
        if (str.find('.') == std::string::npos &&
            str.find('e') == std::string::npos &&
            str.find('E') == std::string::npos) {
            str += ".0";
        }
        result->expr->text = std::move(str);
        return result;
    }

    std::unique_ptr<GLSL::LoweringStmt> ASTGenerator::ProcessExplicitCast(clang::ExplicitCastExpr* expr) {
        if (!expr) return {};

        clang::Expr* subExpr = expr->getSubExpr();
        clang::Expr* subStripped = subExpr ? subExpr->IgnoreUnlessSpelledInSource() : nullptr; //剥离外部的壳，如果没有构造那就cast
        if (subStripped && (llvm::isa<clang::CXXConstructExpr>(subStripped) ||
                            llvm::isa<clang::InitListExpr>(subStripped))) {
            return ProcessClangExpr(subStripped);
        }

        auto sub = ProcessClangExpr(subExpr);
        if (!sub || !sub->expr) return {};

        auto result = std::make_unique<GLSL::LoweringStmt>();
        for (auto& f : sub->fields) {
            result->fields.push_back(std::move(f));
        }
        auto castExpr = std::make_unique<GLSL::Expr>();
        auto [castTypeName, castDims] = SignatureFlattener::FlattenType(expr->getTypeAsWritten());
        castExpr->text = castTypeName + castDims + "(" + sub->expr->text + ")";
        result->expr = std::move(castExpr);
        return result;
    }

    std::unique_ptr<GLSL::LoweringStmt> ASTGenerator::ProcessConstruct(clang::CXXConstructExpr* expr) {
        /*
        构造：
        auto a = A(b);

        GLSL:
        A _temp;
        A a = ctor_A(_temp, b); //这里的_temp 输入是inout ,所有这里的Decl 构造和析构可以直接一样处理即可
        */
        if (!expr) return {};
        if (expr->getConstructor() && expr->getConstructor()->isCopyOrMoveConstructor() &&
            expr->getNumArgs() >= 1) {
            return ProcessClangExpr(expr->getArg(0));
        }
        auto result = std::make_unique<GLSL::LoweringStmt>();

        auto [typeName, dimensions] = SignatureFlattener::FlattenType(expr->getType());
        bool isBuiltin = !dimensions.empty() || typeName != SignatureFlattener::FlattenRecord(expr->getType().getNonReferenceType().getCanonicalType()->getAsCXXRecordDecl());

        //这里代表是内建变量
        if (isBuiltin) {
            auto callExpr = std::make_unique<GLSL::Expr>();
            callExpr->text = typeName + "(";
            bool first = true;
            for (uint32_t i = 0; i < expr->getNumArgs(); ++i) {
                auto arg = ProcessClangExpr(expr->getArg(i));
                if (!arg || !arg->expr) continue;
                for (auto& f : arg->fields) {
                    result->fields.push_back(std::move(f));
                }
                if (!first) callExpr->text += ", ";
                callExpr->text += arg->expr->text;
                first = false;
            }
            callExpr->text += ")";
            result->expr = std::move(callExpr);
            return result;
        }

        //这里需要生成一个临时对象
        auto tempObject = std::make_unique<GLSL::LoweringStmt>();
        auto tempName = "_temp_" + GenerateNumString();
        tempObject->expr = std::make_unique<GLSL::Expr>();
        tempObject->expr->text = typeName + " " + tempName;
        result->fields.push_back(std::move(tempObject));

        if (expr->getConstructor() && ! expr->getConstructor()->isTrivial()) {
            auto cstrExpr = std::make_unique<GLSL::Expr>();
            cstrExpr->text = SignatureFlattener::FlattenFunction(expr->getConstructor()) + "( " + tempName;
            for (uint32_t i = 0; i < expr->getNumArgs(); ++i) {
                auto arg = ProcessClangExpr(expr->getArg(i));
                if (!arg || !arg->expr) continue;
                for (auto& f : arg->fields) {
                    result->fields.push_back(std::move(f));
                }
                cstrExpr->text += "," + arg->expr->text;
            }
            cstrExpr->text += ")";
            auto lastField = std::make_unique<GLSL::LoweringStmt>();
            lastField->expr = std::move(cstrExpr);
            result->fields.push_back(std::move(lastField));
        }

        result->expr = std::make_unique<GLSL::Expr>();
        result->expr->text = tempName;
        return result;
    }

    std::unique_ptr<GLSL::LoweringStmt> ASTGenerator::ProcessInitList(clang::InitListExpr* expr) {
        if (!expr) return {};
        if (auto* semantic = expr->getSemanticForm()) {
            expr = semantic;
        }

        auto result = std::make_unique<GLSL::LoweringStmt>();
        std::string elems;
        bool first = true;
        for (clang::Expr* init : expr->inits()) {
            if (!init || llvm::isa<clang::ImplicitValueInitExpr>(init)) continue;
            auto elemLowering = ProcessClangExpr(init);
            if (!elemLowering || !elemLowering->expr) continue;
            for (auto& f : elemLowering->fields) {
                result->fields.push_back(std::move(f));
            }
            if (!first) elems += ",";
            elems += elemLowering->expr->text;
            first = false;
        }
        auto initExpr = std::make_unique<GLSL::Expr>();
        if (expr->getType()->isRecordType()) {
            auto [typeName, dims] = SignatureFlattener::FlattenType(expr->getType());
            initExpr->text = typeName + dims + "(" + elems + ")";
        } else {
            initExpr->text = "{" + elems + "}";
        }
        result->expr = std::move(initExpr);
        return result;
    }

    std::string ASTGenerator::GenerateNumString() {
        static int num = 0;
        num++;
        return std::to_string(num);
    }

}
