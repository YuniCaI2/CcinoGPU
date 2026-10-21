# ClangAST

此处使用ClangAST主要负责其中Cpp Lambda向GLSL转换，并提取其中的Reflection信息。
具体参考到[CppParser](../../CodeGenerator/CppParser.h).

### 概念记录

ClangAST 中最基础的三个节点大类是 **Decl**（声明）、 **Type**（类型）、 **Stmt**（语句），三者彼此并列又相互引用； **Expr**（表达式）不与
`Stmt` 并列，而是 `Stmt` 的子类——能求值的语句才归入 `Expr`。除此之外，项目里还重度
依赖 **ASTContext / ASTRecordLayout**（查询内存布局，正是文档开头所说"提取 Reflection 信息"的核心机制）和
**TemplateArgument**（模板参数内省，用来识别 `glm::vec`/`glm::mat`/`Accessor` 等模板特化）。

1. **Decl（声明）**：表示一次声明或定义（变量、函数、类型等），是和 `Stmt`、`Type` 并列的第三个基础节点类别。
   `CppParser.cpp` 里最常打交道的子类有 `CXXRecordDecl`（结构体/类）、`ClassTemplateSpecializationDecl`
   （模板特化）、`FunctionDecl`、`VarDecl`、`ValueDecl`，以及通过 `fields()` 遍历得到的 `FieldDecl`。
   [结构体/模板特化识别](../../CodeGenerator/CppParser.cpp#L93)、[Accessor 场景的 ValueDecl 参数](../../CodeGenerator/CppParser.cpp#L137)、[局部声明 VarDecl](../../CodeGenerator/CppParser.cpp#L399)、[全局函数入口 FunctionDecl](../../CodeGenerator/CppParser.cpp#L444)、[字段遍历 FieldDecl](../../CodeGenerator/CppParser.cpp#L514)、[全局变量判定 VarDecl](../../CodeGenerator/CppParser.cpp#L649)
2. **Type（类型 / QualType）**：描述声明、变量和表达式的类型信息；`QualType` 是"指向 Type 节点的指针 + cv/引用等限定符"
   的组合，`CppParser.cpp` 先用 `getNonReferenceType().getCanonicalType().getUnqualifiedType()` 剥掉这层限定和别名，
   取得规范化类型，再分派到内置类型、记录类型或模板特化。[类型转换与模板参数提取](../../CodeGenerator/CppParser.cpp#L77)、[Accessor 类型解析](../../CodeGenerator/CppParser.cpp#L137)、[结构体字段展开](../../CodeGenerator/CppParser.cpp#L502)、[Uniform 类型映射](../../CodeGenerator/CppParser.cpp#L528)
3. **Stmt（语句，`Expr` 是其子类）**：Clang AST 中语句节点的基类，`CompoundStmt`（`{ ... }` 语句块）、`IfStmt`、
   `ForStmt`、`WhileStmt`、`DeclStmt`、`ReturnStmt`、`BreakStmt`、`ContinueStmt`、`NullStmt` 都是它的直接子类；
   `Expr`（可求值的表达式，如变量引用、二元运算、字面量、构造调用、函数调用）则经由 `ValueStmt`
   继承自 `Stmt`，是"语句的一种"而非与 `Stmt` 并列的概念。项目中以 `ProcessSTMT(clang::Stmt*)` 作为统一的语句递归分派入口：
   [语句统一分派入口 ProcessSTMT](../../CodeGenerator/CppParser.cpp#L33)。
   而 `Expr` 子类的递归翻译统一在表达式入口处理——先 `IgnoreUnlessSpelledInSource()` 去掉隐式包装，再用 `dyn_cast`
   判断具体表达式类别：[表达式翻译入口 ChangeCppExprToGLSLExpr](../../CodeGenerator/CppParser.cpp#L180)
4. **ASTContext 与 ASTRecordLayout（上下文与内存布局）**：`ASTContext` 是贯穿一次解析、持有全部 AST 节点的长生命周期
   上下文对象；项目用它查询 `ASTRecordLayout`，拿到 Lambda 捕获类（结构体）里每个字段的字节偏移。这正是文档开头提到
   的"提取 Reflection 信息"的核心机制——没有它就不知道每个 Uniform 字段在 GPU buffer 里的偏移量。
   [Lambda 字段布局与偏移计算](../../CodeGenerator/CppParser.cpp#L627)
5. **TemplateArgument（模板参数内省）**：当记录类型是 `ClassTemplateSpecializationDecl` 时，通过 `getTemplateArgs()`
   拿到 `TemplateArgumentList`，再用 `getAsIntegral()`/`getAsType()`/`getAsDecl()` 读出具体实参，用来识别
   `glm::vec<L,T,Q>`、`glm::mat<C,R,T,Q>`、`Ccino::Array<T,N>`、`Accessor<T,Flags>` 等特化并合成对应 GLSL 类型。
   [vec/mat/array 模板参数解析](../../CodeGenerator/CppParser.cpp#L94)、[Accessor 的 NTTP 参数解析](../../CodeGenerator/CppParser.cpp#L143)、[Uniform 类型里的模板分派](../../CodeGenerator/CppParser.cpp#L551)

### STMT

在 Clang AST 中，`Stmt` 是所有语句与表达式节点的共同基类。`CppParser::ProcessSTMT(clang::Stmt*)`
（[CppParser.cpp#L33](../../CodeGenerator/CppParser.cpp#L33)）通过 `llvm::dyn_cast` / `llvm::isa` 对语句节点做 RTTI
分派，将 C++ 语句递归翻译为 GLSL 代码。CcinoGPU 当前支持的 `Stmt` 子类如下：

1. **CompoundStmt（复合语句 / `{ ... }` 语句块）**
   - **AST 结构**：代表由花括号 `{ ... }` 包裹的语句序列（如函数体、`if`/`for`/`while` 的分支体）。
   - **处理逻辑**：通过 `compoundStmt->body()` 按源码顺序遍历内部所有子语句 `clang::Stmt*`，逐一递归调用
     `ProcessSTMT(subStmt)` 并拼接返回（外层花括号 `{ }` 由上层控制流函数按需包裹）。此外在全局函数转换中，也先将
     `definition->getBody()` 转型为 `CompoundStmt` 再遍历。
   - **代码参考**：[ProcessSTMT 中的 CompoundStmt 递归](../../CodeGenerator/CppParser.cpp#L36)、[全局函数体转型与遍历](../../CodeGenerator/CppParser.cpp#L469)

2. **ReturnStmt（返回语句）**
   - **AST 结构**：表示函数的 `return` 语句。
   - **处理逻辑**：由 `ChangeCppReturnToGLSLReturn` 处理。通过 `returnStmt->getRetValue()` 获取返回值表达式
     `clang::Expr*`：
     - 若非空（带返回值 `return expr;`），调用 `ChangeCppExprToGLSLExpr(expr)` 翻译表达式并拼成 `return <expr>;\n`；
     - 若为空（`void` 函数的裸 `return;`），直接输出 `return ;\n`。
   - **代码参考**：[ProcessSTMT 分派](../../CodeGenerator/CppParser.cpp#L54)、[ChangeCppReturnToGLSLReturn 实现](../../CodeGenerator/CppParser.cpp#L271)

3. **IfStmt（条件分支语句）**
   - **AST 结构**：完整涵盖 `if (init; cond) thenStmt else elseStmt` 四个槽位。
   - **处理逻辑**：由 `ChangeCppIfToGLSLIf` 处理：
     - **拦截 `init` 语句**：先检查 `ifStmt->getInit()`，因为 GLSL 不支持 C++17 的 `if (init; cond)` 语法，非空时报错并返回空串；
     - **条件表达式**：通过 `ifStmt->getCond()` 获取条件 `Expr*`，调用 `ChangeCppExprToGLSLExpr` 翻译为 `if (<cond>)`；
     - **Then 分支**：通过 `ifStmt->getThen()` 获取主分支 `Stmt*`，非空时包裹 `{ \n ... }\n` 并递归调用 `ProcessSTMT(thenStmt)`；
     - **Else 分支**：通过 `ifStmt->getElse()` 获取可选的 `else` 分支 `Stmt*`，非空时包裹 `else {\n ... }\n` 并递归调用
       `ProcessSTMT(elseStmt)`（若源码是 `else if`，`getElse()` 返回的是内嵌的 `IfStmt` 节点，会由 `ProcessSTMT` 递归展开）。
   - **代码参考**：[ProcessSTMT 分派](../../CodeGenerator/CppParser.cpp#L42)、[ChangeCppIfToGLSLIf 实现](../../CodeGenerator/CppParser.cpp#L279)

4. **ForStmt（for 循环语句）**
   - **AST 结构**：表示标准 C 风格 `for (init; cond; inc) body` 循环（注意：C++11 范围 `for (auto x : arr)` 在 Clang AST
     中是独立的 `CXXForRangeStmt`，不继承自 `ForStmt`）。
   - **处理逻辑**：由 `ChangeCppForToGLSLFor` 处理：
     - **语法限制检查**：拒绝范围 for 以及条件区声明变量的写法（通过 `forStmt->getConditionVariableDeclStmt()` 拦截如
       `for (; int x = f(); )`，因 GLSL 不支持在条件表达式里声明变量）；
     - **初始化区 (`getInit()`)**：返回 `Stmt*`（通常是 `DeclStmt` 或赋值 `Expr`），非空时递归调用 `ProcessSTMT(initStmt)`
       （其输出自带 `;`），为空时补 `;`；
     - **条件区 (`getCond()`)**：返回 `Expr*`，非空时调用 `ChangeCppExprToGLSLExpr(condExpr)`，为空时补 `;`；
     - **步进区 (`getInc()`)**：返回 `Expr*`，非空时调用 `ChangeCppExprToGLSLExpr(incStmt)`；
     - **循环体 (`getBody()`)**：包裹在 `) { \n ... }` 中，递归调用 `ProcessSTMT(forStmt->getBody())`。
   - **代码参考**：[ProcessSTMT 分派](../../CodeGenerator/CppParser.cpp#L45)、[ChangeCppForToGLSLFor 实现](../../CodeGenerator/CppParser.cpp#L311)

5. **WhileStmt（while 循环语句）**
   - **AST 结构**：包含条件变量（可选）、条件表达式 `getCond()` 和循环体 `getBody()`。
   - **处理逻辑**：由 `ChangeCppWhileToGLSLWhile` 处理：
     - **语法限制检查**：先检查 `whileStmt->getConditionVariable()`，拦截 `while (int x = f())` 这种在条件中声明变量的写法（GLSL 不支持）；
     - **条件与循环体**：分别用 `ChangeCppExprToGLSLExpr(whileStmt->getCond())` 和 `ProcessSTMT(whileStmt->getBody())`
       递归转换，当条件非空时拼接为 `while (<cond>) {\n<body>}`。
   - **代码参考**：[ProcessSTMT 分派](../../CodeGenerator/CppParser.cpp#L48)、[ChangeCppWhileToGLSLWhile 实现](../../CodeGenerator/CppParser.cpp#L351)

6. **DeclStmt（局部声明语句）**
   - **AST 结构**：连接 `Stmt` 树与 `Decl` 树的桥梁节点。Clang AST 中函数体内的局部变量声明不能直接作为 `Decl` 放进
     `CompoundStmt`，必须外包一层 `DeclStmt`。
   - **处理逻辑**：由 `ChangeCppDeclToGLSLDecl` 与 `ChangeCppInitToGLSLInitializer` 配合完成：
     - **多变量声明拆分**：遍历 `declStmt->decls()`，将每个 `Decl*` 通过 `dyn_cast<clang::VarDecl>` 转为变量声明（非 `VarDecl`
       则报错）。C++ 单行多变量声明（如 `int a = 1, b = 2;`）会被拆成多条独立的 GLSL 声明语句；
     - **Accessor (SSBO) 特判**：先调用 `ChangeAccessorToSSBOReference(var)`，若变量类型是 `Accessor<T, Flags>`，直接生成
       `layout(buffer_reference, std430) readonly/writeonly/readwrite buffer ... <name>;\n`；
     - **auto 推导与数组维度重排**：调用 `ChangeCppTypeToGLSLType(var->getType())` 获取 GLSL 类型（若源码为 `auto`，`var->getType()`
       拿到的已是推导后的真实类型）；若类型含数组维度（如 `float[4]`），将 `[4]` 切分并移到变量名之后，生成符合 GLSL 语法的 `float values[4]`；
     - **初始化器翻译 (`ChangeCppInitToGLSLInitializer`)**：通过 `var->getInit()` 读取初始化表达式并分三类处理：
       1. `InitListExpr`（列表初始化 `{ ... }`）：取 `getSemanticForm()`，遍历 `inits()` 生成 `{e1, e2, ...}`；
       2. `CXXConstructExpr`（构造函数初始化，如 `Vec3 v(1.0f, 2.0f, 3.0f)`）：遍历 `arguments()` 生成 `vec3(1.0, 2.0, 3.0)`；
       3. 普通表达式：回退到 `ChangeCppExprToGLSLExpr` 翻译。
   - **代码参考**：[ProcessSTMT 分派](../../CodeGenerator/CppParser.cpp#L51)、[ChangeCppInitToGLSLInitializer 实现](../../CodeGenerator/CppParser.cpp#L370)、[ChangeCppDeclToGLSLDecl 实现](../../CodeGenerator/CppParser.cpp#L399)

7. **BreakStmt / ContinueStmt / NullStmt（跳转与空语句）**
   - **BreakStmt**：通过 `llvm::isa<clang::BreakStmt>(stmt)` 识别，直接输出 `"break;\n"`（[#L57](../../CodeGenerator/CppParser.cpp#L57)）。
   - **ContinueStmt**：通过 `llvm::isa<clang::ContinueStmt>(stmt)` 识别，直接输出 `"continue;\n"`（[#L60](../../CodeGenerator/CppParser.cpp#L60)）。
   - **NullStmt**：对应源码中的独立空分号 `;`，通过 `llvm::isa<clang::NullStmt>(stmt)` 识别并输出 `";\n"`（[#L63](../../CodeGenerator/CppParser.cpp#L63)）。

8. **Expr（表达式语句与表达式子树）**
   - **AST 结构**：`Expr` 继承自 `ValueStmt : public Stmt`。当表达式独立成句时（如赋值 `a = b + 1;`、自增 `i++;`、函数调用
     `foo();`），在 `ProcessSTMT` 末尾命中 `llvm::dyn_cast<clang::Expr>(stmt)`（[#L66](../../CodeGenerator/CppParser.cpp#L66)），调用
     `ChangeCppExprToGLSLExpr(expr)` 并补上 `";\n"`。
   - **表达式内部递归 (`ChangeCppExprToGLSLExpr`)**：先调用 `expr->IgnoreUnlessSpelledInSource()` 剥离 Clang 自动插入的隐式节点
     （如 `ImplicitCastExpr`、`MaterializeTemporaryExpr` 等），再按具体 `Expr` 子类分派：
     - `DeclRefExpr`（[#L182](../../CodeGenerator/CppParser.cpp#L182)）：变量或符号引用，通过 `getDecl()->getNameAsString()` 提取变量名；
     - `CXXOperatorCallExpr`（[#L187](../../CodeGenerator/CppParser.cpp#L187)）：重载运算符调用（如 `glm::vec` 的 `+`、`=`、`++`、`--`），区分前/后缀自增自减（`getNumArgs()` 为 1 或 2）、中缀二元运算（`isInfixBinaryOp()`）和前缀一元运算；
     - `BinaryOperator`（[#L212](../../CodeGenerator/CppParser.cpp#L212)）：内置标量类型的二元运算，递归拼装 `getLHS()`、`getOpcodeStr()` 与 `getRHS()`；
     - `FloatingLiteral` / `IntegerLiteral` / `CXXBoolLiteralExpr`（[#L219](../../CodeGenerator/CppParser.cpp#L219)）：浮点、整型、布尔字面量转字符串；
     - `CXXConstructExpr`（[#L236](../../CodeGenerator/CppParser.cpp#L236)）：C++ 构造调用（如 `Vec3(1, 2, 3)`），映射为 GLSL 类型构造器 `vec3(1, 2, 3)`；
     - `CallExpr`（[#L246](../../CodeGenerator/CppParser.cpp#L246)）：普通函数调用（如 `glm::dot(a, b)`），通过 `getDirectCallee()->getName()` 去掉命名空间，翻译为 `dot(a, b)`。
   - **代码参考**：[ProcessSTMT 中的 Expr 兜底](../../CodeGenerator/CppParser.cpp#L66)、[ChangeCppExprToGLSLExpr 实现](../../CodeGenerator/CppParser.cpp#L180)

其余未在上述分支处理的 `Stmt` 子类（如 `SwitchStmt`、`DoStmt` 等）在 `ProcessSTMT` 末尾会进入
[Unsupported Stmt 报错分支](../../CodeGenerator/CppParser.cpp#L72)，通过 `stmt->getStmtClassName()` 向 `llvm::errs()`
打印不支持的语句类名并返回空字符串。
