#include "CppParser.h"
#include "ASTGenerator.h"
#include "GLSLAST.h"
#include "ShaderStruct/ShaderCommon.h"
#include "ShaderStruct/UniformData.h"
#include "SignatureFlattener.h"
#include "ShaderStruct/ShaderIR.h"
#include <algorithm>
#include <clang/AST/APValue.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/TemplateBase.h>
#include <clang/AST/Type.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Tooling/ArgumentsAdjusters.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/JSONCompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
#include <fstream>
#include <stdexcept>
#include <iterator>
#include <llvm/Support/Casting.h>
#include <llvm/Support/raw_ostream.h>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace {

    inline std::string ExtractPassName(const clang::CXXMethodDecl* passMethod) {
        if (!passMethod) return {};
        std::string result = "";
        const auto* args = passMethod->getTemplateSpecializationArgs();
        if (!args || args->size() == 0) return {};
        const clang::TemplateArgument& arg0 = args->get(0);

        const clang::APValue* structValPtr = Ccino::ASTGenerator::ExtractStructuralAPValue(arg0);
        if (!structValPtr) {
            llvm::errs() << "Can't Find Lambda In AddPass! \n";
            return {};
        }

        //这里获取FixString
        const clang::APValue& structVal = *structValPtr;
        if (!structVal.isStruct() || structVal.getStructNumFields() == 0) return {};

        const clang::APValue& charArray = structVal.getStructField(0);
        if (!charArray.isArray()) return {};

        uint32_t len = charArray.getArrayInitializedElts(); //得到真实传入的数组的长度，而不是整个char array的长度
        for (unsigned i = 0; i < len; ++i) {
            char c = static_cast<char>(charArray.getArrayInitializedElt(i).getInt().getZExtValue());
            if (c == '\0') break; // 遇到末尾 '\0' 停止
            result.push_back(c);
        }

        return result;
    }

    class ComputePassShaderCollector : public clang::ast_matchers::MatchFinder::MatchCallback {
    public:
        void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override {
            if (!result.Context) return;

            const auto* csLambda      = result.Nodes.getNodeAs<clang::LambdaExpr>("csLambda");
            const auto* addPassMethod = result.Nodes.getNodeAs<clang::CXXMethodDecl>("addPassMethod");
            const auto* addCSMethod   = result.Nodes.getNodeAs<clang::CXXMethodDecl>("addCSMethod");
            if (!csLambda || !addPassMethod || !addCSMethod) return;

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

        Ccino::ThreadGroupSize ExtractGroupSize(const clang::CXXMethodDecl* passMethod) { //成员函数声明
            Ccino::ThreadGroupSize size = {1, 1, 1};
            if (!passMethod) return size;
            const auto* args = passMethod->getTemplateSpecializationArgs();
            if (!args || args->size() == 0) return size;
            const clang::TemplateArgument& arg0 = args->get(0);

            const clang::APValue* structValPtr = Ccino::ASTGenerator::ExtractStructuralAPValue(arg0);
            if (!structValPtr) {
                llvm::errs() << "Can't Find Lambda In AddPass! \n";
                return size;
            }

            //这里获取编译器静态变量
            const clang::APValue& structVal = *structValPtr;
            if (!structVal.isStruct() || structVal.getStructNumFields() < 3) return size;

            size.x = static_cast<uint32_t>(structVal.getStructField(0).getInt().getExtValue());
            size.y = static_cast<uint32_t>(structVal.getStructField(1).getInt().getExtValue());
            size.z = static_cast<uint32_t>(structVal.getStructField(2).getInt().getExtValue());
            return size;
        }

        std::vector<Ccino::ShaderIR> shaderIRs;
    };

    class GraphicsPassShaderCollector : public clang::ast_matchers::MatchFinder::MatchCallback {
        void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override {
            if (!result.Context) return;

            const auto* vsLambda      = result.Nodes.getNodeAs<clang::LambdaExpr>("vsLambda");
            const auto* fsLambda      = result.Nodes.getNodeAs<clang::LambdaExpr>("fsLambda");
            const auto* addPassMethod = result.Nodes.getNodeAs<clang::CXXMethodDecl>("addPassMethod");
            const auto* addVSAndFSMethod   = result.Nodes.getNodeAs<clang::CXXMethodDecl>("addVSAndFSMethod");
            if (!vsLambda || !fsLambda || !addPassMethod || !addVSAndFSMethod) return;

            //需要保证Lambda定义的文件在主要文件中
            if (!result.SourceManager->isWrittenInMainFile(vsLambda->getBeginLoc())) {
                return;
            }

            if (!result.SourceManager->isWrittenInMainFile(fsLambda->getBeginLoc())) {
                return;
            }

            std::string passName = ExtractPassName(addPassMethod);
        }

        std::vector<Ccino::ShaderIR> shaderIRs;
    };

    class LambdaInnerCollector : public clang::RecursiveASTVisitor<LambdaInnerCollector> {
    public:
        LambdaInnerCollector(Ccino::ASTGenerator* astGen) : m_astGenerator(astGen) {}

        bool VisitCallExpr(clang::CallExpr* expr) {
            if (auto* funcDecl = expr->getDirectCallee()) {
                m_astGenerator->InsertFunctionDeclToGraph(funcDecl);
            }

            return true;
        }

        bool VisitDeclStmt(clang::DeclStmt* stmt) {
            for (auto* decl : stmt->decls()) {
                if (auto* var = llvm::dyn_cast<clang::VarDecl>(decl)) {
                    auto canonical = var->getType().getNonReferenceType().getCanonicalType().getUnqualifiedType();
                    if (auto* recordDecl = canonical->getAsCXXRecordDecl()) {
                        m_astGenerator->InsertCXXRecordDeclToGraph(recordDecl);
                    }
                }
            }
            return true;
        }

        Ccino::ASTGenerator* m_astGenerator;
    };
}

namespace Ccino
{
    static constexpr std::string_view kGLSLHeader ="#version 460\n"
                              "#extension GL_EXT_buffer_reference2 : require\n"
                              "#extension GL_EXT_scalar_block_layout : require\n"
                              "#extension GL_EXT_nonuniform_qualifier : require\n"
                              "#extension GL_EXT_shader_image_load_formatted : require\n"
                              "#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require\n";

    // 外部主入口：运行一次 ClangTool 解析该文件，在 MatchCallback 回调里直接把匹配到的所有 Lambda 转成 ShaderIR 返回
    std::vector<ShaderIR> CppParser::ParseFilePassShaders(const std::string& compileCommandJsonPath, std::string* globalFunctions) {
        using namespace clang::ast_matchers;
        std::string errorMsg;

        auto compilationDB = clang::tooling::JSONCompilationDatabase::loadFromFile(
            compileCommandJsonPath, errorMsg, clang::tooling::JSONCommandLineSyntax::AutoDetect);
        if (!compilationDB) {
            llvm::errs() << "Failed to load CompilationDatabase: " << errorMsg << "\n";
            return {};
        }

        auto shouldSkipFile = [](std::string filePath) {
            std::replace(filePath.begin(), filePath.end(), '\\', '/');
            constexpr std::string_view kIgnoredDirs[] = {
                "/ThirdPart/",
                "/CodeGenerator/",
                "/GLSLCompiler/",
                "/RHI/",
                "/Core/",
                "/RG/",
                "/Context/",
            };
            for (std::string_view dir : kIgnoredDirs) {
                if (filePath.find(dir) != std::string::npos) {
                    return true;
                }
            }
            return false;
        };

        auto fileContainsKeyword = [](const std::string& filePath, std::string_view keyword) {
            std::ifstream ifs(filePath, std::ios::binary);
            if (!ifs.is_open()) {
                return false;
            }
            std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            return content.find(keyword) != std::string::npos;
        };

        std::vector<std::string> sourceFiles;
        std::unordered_set<std::string> seenFiles;
        for (const auto& cmd : compilationDB->getAllCompileCommands()) {
            if (shouldSkipFile(cmd.Filename)) {
                continue;
            }
            if (!seenFiles.insert(cmd.Filename).second) {
                continue;
            }
            if (!fileContainsKeyword(cmd.Filename, "AddCS")) {
                continue;
            }
            sourceFiles.push_back(cmd.Filename);
        }
        if (sourceFiles.empty()) {
            return {};
        }

        clang::tooling::ClangTool tool(*compilationDB, sourceFiles);
        tool.appendArgumentsAdjuster(
            [](const clang::tooling::CommandLineArguments& args, llvm::StringRef /*filename*/) {
                clang::tooling::CommandLineArguments adjusted;
                adjusted.reserve(args.size() + 2);
                bool hasResourceDir = false;
                bool afterSeparator = false;
                for (const auto& arg : args) {
                    if (arg == "--") {
                        afterSeparator = true;
                    }
                    if (afterSeparator) {
                        adjusted.push_back(arg);
                        continue;
                    }
                    if (arg == "-resource-dir" || arg.starts_with("-resource-dir=")) {
                        hasResourceDir = true;
                    }
                    if (arg == "-std:c++latest" || arg == "/std:c++latest") {
                        adjusted.push_back("-std:c++20");
                        adjusted.push_back("-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH");
                    } else {
                        adjusted.push_back(arg);
                    }
                }
                if (!hasResourceDir) {
                    // Insert before a possible "--" separator so this remains a driver option.
                    adjusted = clang::tooling::getInsertArgumentAdjuster(
                        "-resource-dir=" CCINO_CLANG_RESOURCE_DIR,
                        clang::tooling::ArgumentInsertPosition::END)(adjusted, "");
                }
                return adjusted;
            });

        ComputePassShaderCollector collector;
        MatchFinder finder;
        //这里 设置Matcher的对象，这里设置了它的调用者和参数绑定
        auto csCallMatcher = cxxMemberCallExpr(
            callee(cxxMethodDecl(hasName("AddCS"), ofClass(hasName("RGBuilder"))).bind("addCSMethod")), // 这里的bind相当于把addCSMethod贴上了标签 
            hasAncestor(cxxMemberCallExpr( //上层调用者 ,并且打上标签
                callee(cxxMethodDecl(hasName("AddPass"), ofClass(hasName("RGBuilder"))).bind("addPassMethod")) 
            )),
            hasArgument(0, anyOf( //检查AddCS的第一个参数
                ignoringImplicit(lambdaExpr().bind("csLambda")), //直接塞进来一个Lambda
                ignoringImplicit(declRefExpr(to(varDecl(hasInitializer(ignoringImplicit(lambdaExpr().bind("csLambda"))))))) //赋值变量表达式 ，从变量引用表达式到变量声明的位置然后得到它的初始化逻辑然后蜕壳得到Lambda
            ))
        );

        auto vsAndFsMatcher = cxxMemberCallExpr(
            callee(cxxMethodDecl(hasName("AddVSAndFS"), ofClass(hasName("RGBuilder"))).bind("addVSAndFSMethod")),
            hasAncestor(cxxMemberCallExpr(
                callee(cxxMethodDecl(hasName("AddPass"), ofClass(hasName("RGBuilder"))).bind("addPassMethod")) 
            )),
            hasArgument(0, anyOf( //检查AddCS的第一个参数
                ignoringImplicit(lambdaExpr().bind("vsLambda")), //直接塞进来一个Lambda
                ignoringImplicit(declRefExpr(to(varDecl(hasInitializer(ignoringImplicit(lambdaExpr().bind("vsLambda"))))))) //赋值变量表达式 ，从变量引用表达式到变量声明的位置然后得到它的初始化逻辑然后蜕壳得到Lambda
            )),
            hasArgument(1, anyOf( //检查AddCS的第一个参数
                ignoringImplicit(lambdaExpr().bind("fsLambda")), //直接塞进来一个Lambda
                ignoringImplicit(declRefExpr(to(varDecl(hasInitializer(ignoringImplicit(lambdaExpr().bind("fsLambda"))))))) //赋值变量表达式 ，从变量引用表达式到变量声明的位置然后得到它的初始化逻辑然后蜕壳得到Lambda
            ))
        );

        finder.addMatcher(csCallMatcher, &collector);

        // 4. 运行 Clang 前端并触发回调
        if (tool.run(clang::tooling::newFrontendActionFactory(&finder).get()) != 0) {
            llvm::errs() << "ClangTool failed on file: " << compileCommandJsonPath << "\n";
            return {};
        }

        if (globalFunctions) {
            *globalFunctions = {}; //在生成代码的时候已经生成了依赖
        }

        // 5. 返回收集好的所有 ShaderIR
        return std::move(collector.shaderIRs);
    }

    ShaderIR CppParser::ProcessComputeShader(const clang::LambdaExpr* lambda, clang::ASTContext& context, const std::string& passName, ThreadGroupSize threadGroupSize) {
        ASTGenerator astGenerator;
        ShaderIR shaderIR;
        shaderIR.shaderType = ShaderType::Compute;
        shaderIR.descriptorSlotLayout = astGenerator.GetDescriptorSlotLayout(lambda, context);
        shaderIR.passName = passName;
        shaderIR.csUniformDataBindings = astGenerator.GetUniformDataBindings(lambda, context);

        std::string localSizeArgs;
        auto appendLocalSize = [&](const char* name, uint32_t size) {
            if (!size) return;
            if (!localSizeArgs.empty()) localSizeArgs += ", ";
            localSizeArgs += std::string(name) + " = " + std::to_string(size);
        };
        appendLocalSize("local_size_x", threadGroupSize.x);
        appendLocalSize("local_size_y", threadGroupSize.y);
        appendLocalSize("local_size_z", threadGroupSize.z);

        // 1. 生成头
        //这里注意scalar不是万能的
        std::string astGlsl = kGLSLHeader.data();
        astGlsl += "layout(" + localSizeArgs + ") in;\n";

        // 2. 生成 Uniform Binding（同时将 Accessor 关联的 BufferReference 结构体及捕获结构体加入 DeclGraph）
        auto uniformBinding = astGenerator.GenerateUniformDataReference(lambda, context);

        // 3. 生成 Struct Decl 和 Function Decl，使用 RecursiveASTVisitor 并且 Emit
        LambdaInnerCollector innerCollector(&astGenerator);
        innerCollector.TraverseStmt(lambda->getBody());
        astGlsl += astGenerator.EmitDeclGraph();

        // 4. Emit Texture / Uniform Binding 与 PushConstant
        GLSL::TextureBinding textureBinding;
        astGlsl += textureBinding.EmitGLSL();
        if (uniformBinding) {
            astGlsl += uniformBinding->EmitGLSL();
        }
        GLSL::PushConstantBinding pushConstantBinding;
        astGlsl += pushConstantBinding.EmitGLSL();

        // 5. 生成 void main 主体
        if (auto mainBody = astGenerator.ProcessClangStmt(lambda->getBody())) {
            astGlsl += "void main() { \n";
            astGlsl += mainBody->EmitGLSL();
            astGlsl += "}\n";
        }

        shaderIR.cs = std::move(astGlsl);
        return shaderIR;
    }


    ShaderIR CppParser::ProcessGraphicsShader(const clang::LambdaExpr* vsLambda, const clang::LambdaExpr* fsLambda, clang::ASTContext& context, const std::string& passName) {
        ASTGenerator astGenerator;
        ShaderIR shaderIR; 
        auto vsDescriptorSlotLayout = astGenerator.GetDescriptorSlotLayout(vsLambda, context);
        auto fsDescriptorSlotLayout = astGenerator.GetDescriptorSlotLayout(fsLambda, context);
        std::string vsGLSL = kGLSLHeader.data();
        std::string fsGLSL = kGLSLHeader.data();

        //合并
        //TODO:修复当前的DescriptorSlotLayout的布局和UniformReference 对齐后者是GLSL 生成的代码前者是CPU端和GPU通信的约定偏移
        std::unordered_map<std::string, UniformData> uniformDataSet;
        DescriptorSlotLayout descriptorSlotLayout{};
        for (auto& data : vsDescriptorSlotLayout) {
            uniformDataSet[data.name] = data; 
        }
        for (auto& data : fsDescriptorSlotLayout) {
            uniformDataSet[data.name] = data; 
        }
        for (auto& [_, data] : uniformDataSet) {
            descriptorSlotLayout.push_back(std::move(data));
        }


        shaderIR.passName = passName;
        shaderIR.vsUniformDataBindings = astGenerator.GetUniformDataBindings(vsLambda, context);
        shaderIR.fsUniformDataBindings = astGenerator.GetUniformDataBindings(fsLambda, context);
        shaderIR.descriptorSlotLayout = descriptorSlotLayout;

        //Uniform Bind
        auto vsUniformBindGLSL = astGenerator.GenerateUniformDataReference(vsLambda, context); 
        auto fsUniformBindGLSL = astGenerator.GenerateUniformDataReference(fsLambda, context);
        auto allUniformBindGLSL = std::make_unique<GLSL::UniformBinding>();
        auto findField = [](const std::unique_ptr<GLSL::UniformBinding>& binding, const std::string& name) -> GLSL::DeclField* {
            if (!binding) {
                return nullptr;
            }
            for (const auto& field : binding->fields) {
                if (field && field->name == name) {
                    return field.get();
                }
            }
            return nullptr;
        };
        for (const auto& data : descriptorSlotLayout) {
            GLSL::DeclField* vsField = findField(vsUniformBindGLSL, data.name);
            GLSL::DeclField* fsField = findField(fsUniformBindGLSL, data.name);
            if (vsField != nullptr && fsField != nullptr &&
                (vsField->type != fsField->type || vsField->dimensions != fsField->dimensions)) {
                throw std::invalid_argument("Descriptor slot field type mismatch between vertex and fragment shaders: " + data.name);
            }
            const GLSL::DeclField* source = vsField != nullptr ? vsField : fsField;
            if (source == nullptr) {
                throw std::invalid_argument("Descriptor slot field is missing from shader bindings: " + data.name);
            }
            auto declField = std::make_unique<GLSL::DeclField>();
            declField->type = source->type;
            declField->name = source->name;
            declField->dimensions = source->dimensions;
            allUniformBindGLSL->fields.push_back(std::move(declField));
        }

        auto vsOut = astGenerator.GenerateOutBinding(vsLambda, context);
        auto fsOut = astGenerator.GenerateOutBinding(fsLambda, context);

        //生成 Struct Decl 和 Function Decl，使用 RecursiveASTVisitor 并且 Emit
        LambdaInnerCollector innerCollector(&astGenerator);
        innerCollector.TraverseStmt(vsLambda->getBody());
        innerCollector.TraverseStmt(fsLambda->getBody());
        auto declGraph = astGenerator.EmitDeclGraph();
        vsGLSL += declGraph;
        fsGLSL += declGraph;

        GLSL::TextureBinding textureBinding;
        vsGLSL += textureBinding.EmitGLSL();
        fsGLSL += textureBinding.EmitGLSL();
        //生成Binding

        if (!allUniformBindGLSL->fields.empty()) {
            vsGLSL += allUniformBindGLSL->EmitGLSL();
            fsGLSL += allUniformBindGLSL->EmitGLSL();
        }

        GLSL::PushConstantBinding pushConstantBinding;
        vsGLSL += pushConstantBinding.EmitGLSL();
        fsGLSL += pushConstantBinding.EmitGLSL();

        vsGLSL += vsOut->EmitGLSL(false);
        // 5. 生成 void main 主体
        if (auto mainBody = astGenerator.ProcessClangStmt(vsLambda->getBody())) {
            vsGLSL += "void main() { \n";
            vsGLSL += mainBody->EmitGLSL();
            vsGLSL += "}\n";
        }

        fsGLSL += vsOut->EmitGLSL(true);
        fsGLSL += fsOut->EmitGLSL(false);
        if (auto mainBody = astGenerator.ProcessClangStmt(fsLambda->getBody())) {
            fsGLSL += "void main() { \n";
            fsGLSL += mainBody->EmitGLSL();
            fsGLSL += "}\n";
        }

        shaderIR.fs = std::move(fsGLSL);
        shaderIR.vs = std::move(vsGLSL);

        return shaderIR;
    }
}  // namespace Ccino
