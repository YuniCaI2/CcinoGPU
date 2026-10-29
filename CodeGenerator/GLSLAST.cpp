#include "GLSLAST.h"

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

    std::string DeclStmt::EmitGLSL() {
        std::string out;
        for (const auto& expr : exprs) {
            if (!expr || expr->text.empty()) continue;
            out += expr->text;
            if (out.back() != ';') out += ';';
            out += '\n';
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
            if (HasLoweringFields(initStmt)) {
                auto* lowering = static_cast<LoweringStmt*>(initStmt.get());
                if (lowering->expr && !lowering->expr->text.empty()) {
                    prologue += EmitLoweringFields(initStmt);
                    initText = lowering->expr->text;
                } else {
                    for (size_t i = 0; i + 1 < lowering->fields.size(); ++i) {
                        prologue += EmitChild(lowering->fields[i]);
                    }
                    if (!lowering->fields.empty()) {
                        initText = TrimTrailingNewlines(EmitChild(lowering->fields.back()));
                    }
                }
            } else {
                initText = TrimTrailingNewlines(EmitChild(initStmt));
            }
        }

        const bool sinkCond = HasLoweringFields(condStmt);
        const bool sinkInc  = HasLoweringFields(incStmt);

        std::string out = prologue;
        out += "for (";
        out += initText;
        if (initText.empty() || initText.back() != ';') out += ';';

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
}
