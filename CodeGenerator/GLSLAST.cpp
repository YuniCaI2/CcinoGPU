#include "GLSLAST.h"
#include <string>

namespace Ccino::GLSL {
    namespace {
        std::string EmitChild(const std::unique_ptr<Stmt>& stmt) {
            return stmt ? stmt->EmitGLSL() : std::string{};
        }

        // if/for/while 条件、for 增量等位置需要表达式文本，不能带语句尾部的 ";\n"
        std::string EmitAsExpr(const std::unique_ptr<Stmt>& stmt) {
            if (!stmt) return {};
            if (stmt->GetKind() == Kind::Expr) {
                return static_cast<Expr*>(stmt.get())->text;
            }
            if (stmt->GetKind() == Kind::LoweringStmt) {
                auto* lowering = static_cast<LoweringStmt*>(stmt.get());
                return lowering->expr ? lowering->expr->text : std::string{};
            }
            std::string text = stmt->EmitGLSL();
            while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) {
                text.pop_back();
            }
            if (!text.empty() && text.back() == ';') {
                text.pop_back();
            }
            return text;
        }

        std::string TrimTrailingNewlines(std::string text) {
            while (!text.empty() && text.back() == '\n') {
                text.pop_back();
            }
            return text;
        }

        bool HasLoweringFields(const std::unique_ptr<Stmt>& stmt) {
            if (!stmt || stmt->GetKind() != Kind::LoweringStmt) return false;
            auto* lowering = static_cast<LoweringStmt*>(stmt.get());
            return !lowering->fields.empty();
        }

        std::string EmitLoweringFields(const std::unique_ptr<Stmt>& stmt) {
            if (!stmt || stmt->GetKind() != Kind::LoweringStmt) return {};
            auto* lowering = static_cast<LoweringStmt*>(stmt.get());
            std::string out;
            for (const auto& field : lowering->fields) {
                out += EmitChild(field);
            }
            return out;
        }

        std::string FormatVarDecl(const VarDecl& decl) {
            if (decl.type.empty() || decl.name.empty()) return {};
            std::string out = decl.type + " " + decl.name + decl.dimensions;
            if (decl.initStmt) {
                std::string initExpr = EmitAsExpr(decl.initStmt);
                if (!initExpr.empty()) {
                    out += " = " + initExpr;
                }
            }
            return out;
        }
    }

    Kind DeclStmt::GetKind() { return Kind::DeclStmt; }
    Kind IfStmt::GetKind() { return Kind::IfStmt; }
    Kind ForStmt::GetKind() { return Kind::ForStmt; }
    Kind WhileStmt::GetKind() { return Kind::WhileStmt; }
    Kind ReturnStmt::GetKind() { return Kind::ReturnStmt; }
    Kind BreakStmt::GetKind() { return Kind::BreakStmt; }
    Kind ContinueStmt::GetKind() { return Kind::ContinueStmt; }
    Kind NullStmt::GetKind() { return Kind::NullStmt; }
    Kind CompoundStmt::GetKind() { return Kind::CompoundStmt; }
    Kind Expr::GetKind() { return Kind::Expr; }
    Kind LoweringStmt::GetKind() { return Kind::LoweringStmt; }

    Kind FunctionDecl::GetKind() { return Kind::FunctionDecl; }
    Kind StructDecl::GetKind() { return Kind::StructDecl; }
    Kind BufferRefDecl::GetKind() { return Kind::BufferRefDecl; }

    std::string Expr::EmitGLSL() {
        return text;
    }

    std::string BreakStmt::EmitGLSL() {
        return text + ";\n";
    }

    std::string ContinueStmt::EmitGLSL() {
        return text + ";\n";
    }

    std::string NullStmt::EmitGLSL() {
        return text + "\n";
    }

    /*
    初始化：
    int temp = 0;
    A a = temp;
    */

    std::string DeclStmt::EmitGLSL() {
        std::string out;
        for (const auto& decl : decls) {
            std::string declText = FormatVarDecl(decl);
            if (declText.empty()) continue;
            if (HasLoweringFields(decl.initStmt)) {
                out += EmitLoweringFields(decl.initStmt);
            }
            out += declText + ";\n";
        }
        return out;
    }

    std::string ReturnStmt::EmitGLSL() {
        if (!returnStmt) {
            return text + ";\n";
        }

        if (returnStmt->GetKind() == Kind::LoweringStmt) {
            auto* lowering = static_cast<LoweringStmt*>(returnStmt.get());
            std::string out;
            for (const auto& field : lowering->fields) {
                out += EmitChild(field);
            }
            out += text;
            if (lowering->expr && !lowering->expr->text.empty()) {
                out += ' ';
                out += lowering->expr->text;
            }
            out += ";\n";
            return out;
        }

        std::string value = EmitAsExpr(returnStmt);
        if (value.empty()) {
            return text + ";\n";
        }
        return text + " " + value + ";\n";
    }

    std::string CompoundStmt::EmitGLSL() {
        std::string out;
        for (const auto& field : fields) {
            if (!field) continue;
            if (field->GetKind() == Kind::Expr) {
                const std::string& exprText = static_cast<Expr*>(field.get())->text;
                if (exprText.empty()) continue;
                out += exprText;
                if (out.back() != ';') out += ';';
                out += '\n';
                continue;
            }
            out += field->EmitGLSL();
        }
        return out;
    }

    std::string LoweringStmt::EmitGLSL() {
        std::string out;
        for (const auto& field : fields) {
            out += EmitChild(field);
        }
        if (expr && !expr->text.empty()) {
            out += expr->text;
            if (out.back() != ';') out += ';';
            out += '\n';
        }
        return out;
    }

    std::string IfStmt::EmitGLSL() {
        // Lowering 产生的前置语句先吐出来，再拼 if
        std::string prologue;
        if (condStmt && condStmt->GetKind() == Kind::LoweringStmt) {
            auto* lowering = static_cast<LoweringStmt*>(condStmt.get());
            for (const auto& field : lowering->fields) {
                prologue += EmitChild(field);
            }
        }

        std::string out = prologue;
        out += "if (";
        out += EmitAsExpr(condStmt);
        out += ") {\n";
        out += EmitChild(thenStmt);
        out += "}\n";
        if (elseStmt) {
            out += "else {\n";
            out += EmitChild(elseStmt);
            out += "}\n";
        }
        return out;
    }

    std::string ForStmt::EmitGLSL() {
        std::string prologue;
        std::string initText;

        if (initStmt) {
            std::string raw = TrimTrailingNewlines(initStmt->EmitGLSL()); //这里去掉了末尾的\n
            if (!raw.empty() && raw.back() == ';') {
                raw.pop_back();
            }
            const size_t lastNewline = raw.rfind('\n');
            //这里非常的巧妙，倒着找到最后一个\n的index 也就是倒数第二个\n
            if (lastNewline != std::string::npos) {
                prologue = raw.substr(0, lastNewline + 1);
                initText = raw.substr(lastNewline + 1);
            } else {
                initText = std::move(raw);
            }
        }

        const bool sinkCond = HasLoweringFields(condStmt);
        const bool sinkInc  = HasLoweringFields(incStmt);

        std::string out = prologue;
        out += "for (";
        out += initText;
        out += ';';

        if (!sinkCond) {
            out += EmitAsExpr(condStmt);
        }
        out += ';';

        if (!sinkInc) {
            out += EmitAsExpr(incStmt);
        }
        out += ") {\n";

        if (sinkCond) {
            out += EmitLoweringFields(condStmt);
            std::string condExpr = EmitAsExpr(condStmt);
            if (!condExpr.empty()) {
                out += "if (!(" + condExpr + ")) break;\n";
            }
        }

        out += EmitChild(body);

        if (sinkInc) {
            out += EmitChild(incStmt);
        }

        out += "}\n";
        return out;
    }

    std::string WhileStmt::EmitGLSL() {
        const bool sinkCond = HasLoweringFields(condStmt);

        std::string out;
        out += "while (";
        out += sinkCond ? "true" : EmitAsExpr(condStmt);
        out += ") {\n";

        if (sinkCond) {
            out += EmitLoweringFields(condStmt);
            std::string condExpr = EmitAsExpr(condStmt);
            if (!condExpr.empty()) {
                out += "if (!(" + condExpr + ")) break;\n";
            }
        }

        out += EmitChild(body);
        out += "}\n";
        return out;
    }

    std::string DeclField::EmitGLSL() {
        return type + " " + name + dimensions; //这里的dimension默认有[]  
    }

    std::string FunctionDecl::EmitGLSL() {
        std::string out = {};
        out += returnType;
        out += " ";
        out += name + " (";
        for (int i = 0; i < fields.size(); i++) {
            out += !i ? fields[i]->EmitGLSL() : ", " + fields[i]->EmitGLSL();
        }
        out += ") { \n";
        out += body->EmitGLSL();
        out += "}\n";

        return out;        
    }

    std::string StructDecl::EmitGLSL() {
        std::string out = {};
        if (isBufferReference) {
            out += "layout(buffer_reference, scalar) buffer " + name + "{ \n";
            for (int i = 0; i < fields.size(); i++) {
                out += fields[i]->EmitGLSL() + ";\n";
            }
            out += "};\n";
        } else {
            out += "struct " + name + "{\n";
            for (int i = 0; i < fields.size(); i++) {
                out += fields[i]->EmitGLSL() + ";\n";
            }
            out += "};\n";
        }
        return out;
    }

    std::string UniformBinding::EmitGLSL() {
        std::string out =
            "layout(buffer_reference, scalar, buffer_reference_align = 8) readonly buffer UniformData { \n";
        for (int i = 0; i < fields.size(); i++) {
            out += fields[i]->EmitGLSL() + ";\n";
        }
        out += "};\n";

        return out;
    }

    std::string PushConstantBinding::EmitGLSL() {
        return "layout(push_constant) uniform PC {\n"
               "    UniformData uniformData;\n"
               "} pc;\n";
    }

    std::string TextureBinding::EmitGLSL() {
        return "layout(set = 0, binding = 0) uniform sampler globalSamplers[];\n"
        "layout(set = 0, binding = 1) uniform texture2D globalTextures[];\n"
        "layout(set = 0, binding = 1) uniform itexture2D globalITextures[];\n"
        "layout(set = 0, binding = 1) uniform utexture2D globalUTextures[];\n"
        "layout(set = 0, binding = 2) uniform image2D globalImages[];\n"
        "layout(set = 0, binding = 2) uniform iimage2D globalIImages[];\n"
        "layout(set = 0, binding = 2) uniform uimage2D globalUImages[];\n";
    }

    std::string OutBinding::EmitGLSL(bool in) {
        std::string result ={};
        for (int i = 0; i < fields.size(); i++) {
            auto& field = fields[i];
            std::string inout = (in ? "in" : "out");
            result += "layout(location = "+ std::to_string(i) +") " + inout + " " + field->EmitGLSL() + ";\n";
        }
        return result;
    }
}
