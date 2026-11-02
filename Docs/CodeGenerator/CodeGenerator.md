# CodeGenerator 架构设计（Clang AST -> GLSL AST 转译器）

## 1. 概述与设计演进

`CodeGenerator` 负责使用 Clang LibTooling 解析 C++ 源码中的 Shader Lambda、全局函数与自定义结构体，将其降级（Lowering）为 Vulkan GLSL 460 代码，并提取 `DescriptorSlotLayout` 与 `ShaderUniformDataBinding` 反射信息。

早期的 `CppParser` 采用**语法制导直译（Syntax-Directed Pretty Printer）**模式：一边递归遍历 Clang AST，一边拼接 `std::string`，最后通过正则表达式替换内建变量和捕获变量。该模式仅在 C++ 代码与 GLSL 1:1 同构（纯 C 子集）时可用，无法处理：
1. **单表达式裂变为多条前置语句**（如非平凡构造函数 `A(B(x))` 需拆分为临时变量声明 + `ctor` 函数调用）；
2. **C++ 作用域、模板特化、函数重载与成员函数**向 GLSL 扁平全局命名空间的映射；
3. **结构体与函数之间的依赖拓扑排序**；
4. **同名局部变量/成员字段对正则文本替换的误伤**。

重构后的架构采用标准的**四层源到源转译器（Source-to-Source Transpiler）分层**：

```
Clang LibTooling (Frontend AST)
        │
        ├──► SignatureFlattener    : 作用域/重载/模板签名展平，数组维度剥离 (FlattenedType)
        │
        ├──► ASTGenerator          : 语义降级与依赖收集
        │      ├─ ASTGenerator_Expr.cpp  (Expr Lowering -> GLSL::LoweringStmt)
        │      ├─ ASTGenerator_Stmt.cpp  (Stmt Lowering -> GLSL::Stmt 子树)
        │      ├─ ASTGenerator_Decl.cpp  (Struct/Function 入图 -> GLSL::DeclGraph + Kahn 拓扑排序)
        │      └─ ASTGenerator_IRGen.cpp (Lambda 捕获布局反射 -> DescriptorSlotLayout / UniformDataBindings)
        │
        └──► GLSLAST (Target IR)   : 控制流前置/下沉排布，统一调用 EmitGLSL() 产出 GLSL 源码
```

---

## 2. 模块职责与核心数据结构

### 2.1 `SignatureFlattener`：符号编码与类型展平

源码位于 [SignatureFlattener.h](../../CodeGenerator/SignatureFlattener.h) / [SignatureFlattener.cpp](../../CodeGenerator/SignatureFlattener.cpp)。

GLSL 没有命名空间、类作用域、模板实参和函数重载（仅支持全局函数重载，不支持跨作用域同名）。`SignatureFlattener` 将 C++ 声明与类型统一编码为合法的 GLSL 标识符（同时折叠连续双下划线 `__` 以遵守 GLSL 规范保留字要求）：

| 接口 | 输入示例 | 输出结果 | 说明 |
|---|---|---|---|
| `Flatten(const NamedDecl*)` | `Physics::RigidBody` | `"Physics_RigidBody"` | 自外向内拼接命名空间、外层类与自身名称（跳过匿名结构体与 Lambda 闭包类） |
| `FlattenRecord(const RecordDecl*)` | `Sim::Packet<float, 4>` | `"Sim_Packet_float_4"` | 对 `ClassTemplateSpecializationDecl` 自动追加模板实参后缀 |
| `FlattenFunction(const FunctionDecl*, bool includeParams)` | `void RigidBody::Eval(int) const` | `"Physics_RigidBody_Eval_int_const"` | 构造函数映射为 `ctor`，重载运算符映射为 `op_plus` 等，追加形参类型与 `const` 后缀区分重载 |
| `FlattenType(QualType)` | `Ccino::Array<float, 4>[2]` | `FlattenedType{"float", "[2][4]"}` | 剥离外层 C 数组 `T[N]` 与 `std::array<T, N>` 维度至 `dimensions`，并将底层元素类型映射为 GLSL 类型名（如 `glm::vec<3, float>` -> `"vec3"`） |

---

### 2.2 `GLSLAST`：目标 GLSL 中间表示（Target AST）

源码位于 [GLSLAST.h](../../CodeGenerator/GLSLAST.h) / [GLSLAST.cpp](../../CodeGenerator/GLSLAST.cpp)。

`GLSLAST` 采用**“语句与声明建完整树，纯表达式内联文本”**的折中设计：

#### （1）核心降级容器：`GLSL::LoweringStmt`
```cpp
struct LoweringStmt : Stmt {
    std::vector<std::unique_ptr<Stmt>> fields; // 必须在当前表达式求值前执行的前置语句
    std::unique_ptr<Expr> expr;                // 当前表达式求值后的结果文本（语句节点时可为空）
};
```
当一个 C++ 子表达式需要拆解出临时变量声明或初始化调用时，前置语句被推入 `fields`，而最终可被引用的标识符或表达式保存在 `expr->text` 中。

#### （2）语句节点（`GLSL::Stmt` 子类）的 `EmitGLSL()` 前置/下沉排布规则
- **`DeclStmt`**：遍历 `std::vector<VarDecl>`。若某个变量的初始化器 `initStmt` 带有 `LoweringStmt::fields`（如自定义结构体构造），先将 `fields` 吐在变量声明语句之前，再输出 `<type> <name><dimensions> = <initExpr>;\n`。
- **`ReturnStmt`**：若返回值 `returnStmt` 含有 `fields` 前置语句，先在 `return` 之前输出所有 `fields`，最后输出 `return <expr>;\n`。
- **`IfStmt`**：若条件表达式 `condStmt` 含有 `fields` 前置语句，先在 `if (...)` 之前作为 `prologue` 吐出，再输出 `if (<cond>) { ... } else { ... }`。
- **`ForStmt`**：
  - 若 `initStmt` 带有前置语句，自动切分最后一个 `\n`，将前置语句提至 `for (` 之前作為 `prologue`，仅将最后一条声明/表达式留在 `for (init; ...)` 槽位内；
  - 若条件 `condStmt` 带有前置语句（`sinkCond`），则每次迭代都需重新求值，不能放在 `for` 外面：自动将 `for` 条件槽置空，在循环体开头插入 `fields` + `if (!(<cond>)) break;\n`；
  - 若步进 `incStmt` 带有前置语句（`sinkInc`），将步进槽置空，并下沉至循环体末尾执行。
- **`WhileStmt`**：若条件 `condStmt` 带有前置语句（`sinkCond`），生成 `while (true) {\n <fields> if (!(<cond>)) break;\n <body> }\n`。

#### （3）声明节点与依赖图（`GLSL::Decl` 与 `GLSL::DeclGraph`）
- **`DeclField`**：表示结构体成员变量或函数形参（`type + " " + name + dimensions`）。
- **`StructDecl`**：表示结构体定义或 BDA SSBO 缓冲引用块（由 `isBufferReference` 区分 `struct Name { ... };` 与 `layout(buffer_reference, std430) buffer Name { ... };`）。
- **`FunctionDecl`**：包含 `returnType`、`name`、形参列表 `fields`、函数体 `body` 以及函数体内部依赖的自定义类型列表 `depDeclNames`。
- **`DeclGraph`**：保存所有已收集声明节点的哈希表 `nodes` 与依赖邻接表 `edges`。

---

### 3. `ASTGenerator` 四大子模块

头文件定义于 [ASTGenerator.h](../../CodeGenerator/ASTGenerator.h)，实现按职责拆分为四个源文件：

### 3.1 表达式降级（`ASTGenerator_Expr.cpp`）
入口为 `ASTGenerator::ProcessClangExpr(clang::Expr*)`（[ASTGenerator_Expr.cpp](../../CodeGenerator/ASTGenerator_Expr.cpp)）：
1. **隐式节点剥离**：调用 `expr->IgnoreUnlessSpelledInSource()` 穿透隐式类型转换与临时对象包装。
2. **上下文感知符号替换（消除正则替换）**：
   - **`CXXThisExpr`**：直接映射为 `"self"`；
   - **`DeclRefExpr`**：
     - 若 `refersToEnclosingVariableOrCapture()` 或 `varDecl->isInitCapture()` 为真，判定为 Lambda 捕获变量，直接生成 `"pc.uniformData." + name`；
     - 若命中 `GLSLBuiltinVarMap`（且非局部变量/形参），直接映射为 GLSL 内建变量（如 `Ccino::ComputeGlobalID` -> `"gl_GlobalInvocationID"`）；
     - 其余保持局部标识符名。
   - **`MemberExpr`（`ProcessMemberExpr`）**：
     - 若字段名为空（如 `glm::vec` 内部的匿名 `union` / `struct` 成员访问 `v.x`），直接穿透到 `getBase()`；
     - 若 `isImplicitAccess()` 为真（类成员函数内隐式访问成员变量），结合 `m_currentStructDecl` 状态转为 `"self." + name`。
3. **成员函数调用（`ProcessMemberCallExpr`）**：
   - 将隐式/显式对象实参（`self` 或 `obj`）作为首个实参，方法名通过 `SignatureFlattener::FlattenFunction(method, true)` 展平，生成 `Namespace_Class_Method_ParamTypes(obj, args...)`。
4. **构造函数降级（`ProcessConstruct`）**：
   - **拷贝/移动构造**：直接透传首个实参 `ProcessClangExpr(expr->getArg(0))`；
   - **GLSL 内建数学类型（`vec` / `mat` 等）**：直接生成 `vec3(arg0, arg1, ...)`；
   - **用户自定义结构体**：
     1. 分配递增临时变量名 `_temp_N`，在 `result->fields` 中压入声明语句 `<TypeName> _temp_N`；
     2. 若构造函数非平凡（`!isTrivial()`），在 `result->fields` 中压入构造函数调用 `<FlattenedCtorName>(_temp_N, args...)`；
     3. 将 `result->expr->text` 设为 `_temp_N` 供外层表达式消费。
5. **列表初始化（`ProcessInitList`）**：
   - 取 `getSemanticForm()` 过滤隐式空值初始化；
   - 若类型为记录类型（聚合 `struct` 或 `std::array`），生成 `<typeName><dims>(<elems>)`；
   - 若为 C 原生定长数组，生成 `{<elems>}`。

### 3.2 语句降级（`ASTGenerator_Stmt.cpp`）
入口为 `ASTGenerator::ProcessClangStmt(clang::Stmt*)`（[ASTGenerator_Stmt.cpp](../../CodeGenerator/ASTGenerator_Stmt.cpp)）：
- 负责将 `CompoundStmt`、`DeclStmt`、`IfStmt`、`ForStmt`、`WhileStmt`、`ReturnStmt`、`BreakStmt`、`ContinueStmt`、`NullStmt` 及独立成句的 `Expr` 转换为对应的 `GLSL::Stmt` 节点并封装进 `GLSL::LoweringStmt`。
- 在 `ProcessDeclStmt` 中：
  - 拦截函数体内非法声明的 `Accessor` 变量；
  - 使用 `IsTrivialEmptyInit` 识别无参默认构造或空 `{}` 聚合初始化，避免生成非法的空参构造器调用 `Type()`；
  - 通过 `SignatureFlattener::FlattenType` 一次性拆出基础类型名与多维数组后缀。

### 3.3 声明入图与拓扑排序（`ASTGenerator_Decl.cpp`）
实现于 [ASTGenerator_Decl.cpp](../../CodeGenerator/ASTGenerator_Decl.cpp)：
1. **`InsertCXXRecordDeclToGraph(clang::CXXRecordDecl*)`**：
   - 获取完整定义 `recordDecl->getDefinition()`（忽略前向声明）；
   - 通过 `SignatureFlattener::FlattenRecord` 生成结构体名，遍历 `def->fields()` 调用 `FlattenType` 提取每个成员的 `type`、`name`、`dimensions`；
   - 调用 `InsertGLSLStruct` 写入 `m_declGraph.nodes` 并将字段类型记录到 `m_declGraph.edges`。
2. **`InsertFunctionDeclToGraph(clang::FunctionDecl*)`**：
   - 对成员函数（`CXXMethodDecl`），在形参列表首部插入 `inout <ClassName> self`，并在转换函数体期间设置 `m_currentStructDecl = parent`，转换结束后恢复为 `nullptr`；
   - 对引用/指针形参自动添加 `inout ` 修饰符；
   - 通过 `DepDeclCollector`（继承自 `clang::RecursiveASTVisitor`）遍历函数体内的 `DeclStmt`，收集局部自定义结构体依赖至 `depDeclNames`；
   - 调用 `InsertGLSLFunc` 写入 `m_declGraph`。
3. **`SortDeclGraph()`**：
   - 基于 Kahn 算法对 `m_declGraph` 执行按层拓扑排序，自动跳过不在 `m_declGraph.nodes` 中的 GLSL 内建类型（如 `float`、`vec3`）及 `inout ` 前缀，返回按依赖层级排列的节点名称二维数组 `std::vector<std::vector<std::string>>`。

### 3.4 反射与绑定布局提取（`ASTGenerator_IRGen.cpp`）
实现于 [ASTGenerator_IRGen.cpp](../../CodeGenerator/ASTGenerator_IRGen.cpp)：
- **`GetDescriptorSlotLayout`**：遍历 Lambda 捕获类 `lambdaClass->fields()`（兼顾广义初始化捕获与普通变量捕获），将每个捕获字段的 `QualType` 通过 `GetUniformDataType` 映射为 `UniformDataType`（`Buffer`、标量、向量、矩阵），生成 `DescriptorSlotLayout`。
- **`GetUniformDataBindings`**：通过 `context.getASTRecordLayout(lambdaClass)` 查询每个捕获字段在闭包结构体中的位偏移并换算为字节偏移（`getFieldOffset(fieldIndex) / 8`），生成 CPU 与 GPU 对齐的 `ShaderUniformDataBinding` 列表。

---

## 4. 测试体系

针对重构后的各层模块，`Tests/` 下设有独立的 GoogleTest 测试套件：
- **`ProcessClangExprTest`**（[Tests/test_process_clang_expr.cpp](../../Tests/test_process_clang_expr.cpp)）：覆盖算术/位/逻辑/三元运算、GLM 向量矩阵重载运算与下标访问、显式类型转换、内置与自定义结构体构造（含嵌套构造与临时变量展开）、函数重载与默认参数补全、结构体成员函数调用及 Lambda 捕获替换。
- **`ProcessClangStmtTest`**（[Tests/test_process_clang_stmt.cpp](../../Tests/test_process_clang_stmt.cpp)）：覆盖 `CompoundStmt`、`DeclStmt`（多变量拆分、C 数组、`std::array`、聚合初始化、非法 `Accessor` 拦截）、`ReturnStmt`、`IfStmt`、`WhileStmt`、`ForStmt` 以及完整 Shader Kernel 函数体生成。
- **`ProcessClangDeclTest`**（[Tests/test_process_clang_decl.cpp](../../Tests/test_process_clang_decl.cpp)）：覆盖 `InsertCXXRecordDeclToGraph` 与 `InsertFunctionDeclToGraph` 的单测及 `DeclGraph` 依赖图集成测试。
