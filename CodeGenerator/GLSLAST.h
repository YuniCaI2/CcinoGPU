#pragma once
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>

namespace Ccino::GLSL {
    enum class Kind {
        //Stmt
        DeclStmt,
        IfStmt,
        ForStmt,
        WhileStmt,
        ReturnStmt,
        BreakStmt,
        ContinueStmt,
        NullStmt,
        CompoundStmt,
        Expr,
        LoweringStmt,

        //Decl
        FunctionDecl,
        StructDecl,
        BufferRefDecl,
    };
    struct Expr;

    struct Stmt {
        virtual ~Stmt() = default;
        virtual std::string EmitGLSL() = 0;
        virtual Kind GetKind() = 0;
    };

    struct VarDecl {
        std::string type;
        std::string name;
        std::string dimensions;
        std::unique_ptr<Stmt> initStmt;
    };

    struct DeclStmt : Stmt {
        Kind GetKind() override;
        std::string EmitGLSL() override;
        std::vector<VarDecl> decls;
    };

    struct IfStmt : Stmt {
        Kind GetKind() override;
        std::string EmitGLSL() override;
        std::unique_ptr<Stmt> condStmt;
        std::unique_ptr<Stmt> thenStmt;
        std::unique_ptr<Stmt> elseStmt;
    };

    struct ForStmt : Stmt {
        Kind GetKind() override;
        std::string EmitGLSL() override;
        std::unique_ptr<Stmt> initStmt;
        std::unique_ptr<Stmt> condStmt;
        std::unique_ptr<Stmt> incStmt;
        std::unique_ptr<Stmt> body;
    };


    struct WhileStmt : Stmt {
        Kind GetKind() override;
        std::string EmitGLSL() override;
        std::unique_ptr<Stmt> condStmt;
        std::unique_ptr<Stmt> body;
    };

    struct ReturnStmt : Stmt {
        Kind GetKind() override;
        std::string EmitGLSL() override;
        std::string text = "return";
        std::unique_ptr<Stmt> returnStmt;
    };

    struct BreakStmt : Stmt {
        Kind GetKind() override;
        std::string EmitGLSL() override;
        std::string text = "break";
    };

    struct ContinueStmt : Stmt {
        Kind GetKind() override;
        std::string EmitGLSL() override;
        std::string text = "continue";
    };

    struct NullStmt : Stmt {
        Kind GetKind() override;
        std::string EmitGLSL() override;
        std::string text = ";";
    };

    struct CompoundStmt : Stmt {
        Kind GetKind() override;
        std::string EmitGLSL() override;
        std::vector<std::unique_ptr<Stmt>> fields;
    };

    struct Expr : Stmt {
        Kind GetKind() override;
        std::string EmitGLSL() override;
        std::string text;
    };


    struct LoweringStmt : Stmt {
        Kind GetKind() override;
        std::string EmitGLSL() override;
        std::vector<std::unique_ptr<Stmt>> fields;
        std::unique_ptr<Expr> expr; //现在
    };

    struct Decl {
        virtual ~Decl() = default;
        virtual Kind GetKind() = 0;
        std::string name;
    };

    struct DeclField {
        std::string type;
        std::string name;
    };

    struct FunctionDecl : Decl {
        virtual Kind GetKind() override;
        std::string returnType;
        std::vector<std::unique_ptr<DeclField>> fields;
        std::unique_ptr<Stmt> body;
    };


    struct StructDecl : Decl {
        virtual Kind GetKind() override;
        std::vector<std::unique_ptr<DeclField>> fields;
    };

    struct BufferRefDecl : StructDecl {
        virtual Kind GetKind() override;
    };


    //这是严格的DAG
    struct DeclGraph {
        std::unordered_map<std::string, std::unique_ptr<Decl>> nodes;
        std::unordered_map<std::string, std::vector<std::string>> edges;
    };

}