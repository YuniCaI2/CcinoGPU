#include <gtest/gtest.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/Stmt.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/Tooling.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "CodeGenerator/ASTGenerator.h"

namespace
{
    using namespace clang;
    using namespace clang::ast_matchers;

    // 收集 TestEntry 函数体内的所有顶层表达式语句（以及 VarDecl 的初始化表达式）并调用 ASTGenerator::ProcessClangExpr
    class TopLevelExprCollector : public MatchFinder::MatchCallback
    {
    public:
        std::vector<std::string> ExprTexts;
        std::vector<std::string> EmittedStmts;
        std::unordered_map<std::string, std::string> VarInitExprTexts;

        void run(const MatchFinder::MatchResult& Result) override {
            const auto* Func = Result.Nodes.getNodeAs<FunctionDecl>("func");
            if (!Func || !Func->hasBody()) return;

            Ccino::ASTGenerator generator;
            if (const auto* Method = llvm::dyn_cast<CXXMethodDecl>(Func)) {
                CXXRecordDecl* parent = const_cast<CXXRecordDecl*>(Method->getParent());
                if (parent && !parent->isLambda()) {
                    generator.SetCurrentStructDecl(parent);
                }
            }

            auto* compound = llvm::dyn_cast<CompoundStmt>(Func->getBody());
            if (!compound) return;

            for (Stmt* subStmt : compound->body()) {
                if (auto* declStmt = llvm::dyn_cast<DeclStmt>(subStmt)) {
                    for (Decl* decl : declStmt->decls()) {
                        auto* varDecl = llvm::dyn_cast<VarDecl>(decl);
                        if (!varDecl || !varDecl->hasInit()) continue;
                        auto lowering = generator.ProcessClangExpr(varDecl->getInit());
                        if (lowering && lowering->expr) {
                            VarInitExprTexts[varDecl->getNameAsString()] = lowering->expr->text;
                        }
                    }
                    continue;
                }

                if (auto* expr = llvm::dyn_cast<Expr>(subStmt)) {
                    auto lowering = generator.ProcessClangExpr(expr);
                    if (lowering && lowering->expr) {
                        ExprTexts.push_back(lowering->expr->text);
                        EmittedStmts.push_back(lowering->EmitGLSL());
                    } else {
                        ExprTexts.emplace_back();
                        EmittedStmts.emplace_back();
                    }
                }
            }
        }
    };

    TopLevelExprCollector RunProcessClangExprOnCode(const std::string& code,
                                                    const std::string& entryFuncName = "TestEntry") {
        TopLevelExprCollector collector;
        MatchFinder finder;

        finder.addMatcher(functionDecl(hasName(entryFuncName), isDefinition()).bind("func"), &collector);

        EXPECT_TRUE(
            tooling::runToolOnCodeWithArgs(tooling::newFrontendActionFactory(&finder)->create(), code, {"-std=c++20"}));
        return collector;
    }

    const std::string kExprTestPreamble = R"(
        using uint32_t = unsigned int;

        template <typename BitType>
        struct Flags {
            uint32_t mask = 0;
            constexpr Flags() = default;
            constexpr Flags(BitType bit) : mask(static_cast<uint32_t>(bit)) {}
            constexpr explicit Flags(uint32_t flags) : mask(flags) {}
        };

        enum class BufferAccessFlagBits : uint32_t {
            Read = 1 << 0,
            Write = 1 << 1,
            ReadWrite = Read | Write,
            None = 0
        };
        using BufferAccessFlags = Flags<BufferAccessFlagBits>;

        namespace std {
            template<typename T, unsigned long long N>
            struct array {
                T elems[N];
                T& operator[](unsigned long long i) { return elems[i]; }
                const T& operator[](unsigned long long i) const { return elems[i]; }
            };
        }

        namespace glm {
            enum qualifier { defaultp };

            template<int L, typename T, qualifier Q = defaultp>
            struct vec {
                union {
                    struct { T x, y, z, w; };
                    struct { T r, g, b, a; };
                };
                vec() = default;
                vec(T s) : x(s), y(s), z(s), w(s) {}
                vec(T x_, T y_) : x(x_), y(y_), z{}, w{} {}
                vec(T x_, T y_, T z_) : x(x_), y(y_), z(z_), w{} {}
                vec(T x_, T y_, T z_, T w_) : x(x_), y(y_), z(z_), w(w_) {}

                T& operator[](int i) { return x; }
                const T& operator[](int i) const { return x; }

                vec operator+(const vec& rhs) const { return *this; }
                vec operator-(const vec& rhs) const { return *this; }
                vec operator*(const vec& rhs) const { return *this; }
                vec operator/(const vec& rhs) const { return *this; }
                vec& operator+=(const vec& rhs) { return *this; }
                vec& operator-=(const vec& rhs) { return *this; }
                vec& operator*=(const vec& rhs) { return *this; }
                vec& operator/=(const vec& rhs) { return *this; }

                vec operator-() const { return *this; }
                vec operator~() const { return *this; }
                vec operator!() const { return *this; }

                vec& operator++() { return *this; }
                vec operator++(int) { return *this; }
                vec& operator--() { return *this; }
                vec operator--(int) { return *this; }

                bool operator==(const vec& rhs) const { return true; }
                bool operator!=(const vec& rhs) const { return false; }
            };

            template<int C, int R, typename T, qualifier Q = defaultp>
            struct mat {
                mat() = default;
                mat operator*(const mat& rhs) const { return *this; }
            };

            float dot(const vec<3, float>& a, const vec<3, float>& b) { return 0.0f; }
            vec<3, float> cross(const vec<3, float>& a, const vec<3, float>& b) { return {}; }
            vec<3, float> normalize(const vec<3, float>& v) { return v; }
            int clamp(int v, int lo, int hi) { return v; }
            int floatBitsToInt(float v) { return 0; }
            void barrier() {}
        }

        namespace Ccino {
            using Vec2 = glm::vec<2, float>;
            using Vec3 = glm::vec<3, float>;
            using Vec4 = glm::vec<4, float>;
            using IVec3 = glm::vec<3, int>;
            using UVec3 = glm::vec<3, unsigned int>;
            using BVec3 = glm::vec<3, bool>;
            using Mat4 = glm::mat<4, 4, float>;

            inline constexpr UVec3 ComputeNumWorkGroups{};
            inline constexpr UVec3 ComputeWorkGroupSize{};
            inline constexpr UVec3 ComputeWorkGroupID{};
            inline constexpr UVec3 ComputeLocalID{};
            inline constexpr UVec3 ComputeLocalInvocationID{};
            inline constexpr UVec3 ComputeGlobalID{};
            inline constexpr UVec3 ComputeGlobalInvocationID{};
            inline constexpr uint32_t ComputeLocalIndex{};
            inline constexpr uint32_t ComputeLocalInvocationIndex{};

            template<typename T, unsigned long long N>
            using Array = std::array<T, N>;

            template<typename BufferResourceType, BufferAccessFlags bufferAccessFlags>
            class Accessor {
            public:
                BufferResourceType& operator[](uint32_t index) const {
                    static BufferResourceType dummy{};
                    return dummy;
                }
            };

            template <typename... Args>
            constexpr decltype(auto) Dot(Args&&... args) {
                return glm::dot(args...);
            }

            template <typename... Args>
            constexpr decltype(auto) Cross(Args&&... args) {
                return glm::cross(args...);
            }

            template <typename... Args>
            constexpr decltype(auto) Normalize(Args&&... args) {
                return glm::normalize(args...);
            }

            template <typename... Args>
            constexpr decltype(auto) Clamp(Args&&... args) {
                return glm::clamp(args...);
            }

            template <typename... Args>
            constexpr decltype(auto) FloatBitsToInt(Args&&... args) {
                return glm::floatBitsToInt(args...);
            }
        }
    )";
}  // namespace

// ============================================================================
// 1. 空指针与基础字面量 / 变量引用测试
// ============================================================================

TEST(ProcessClangExprTest, NullPointerReturnsNull) {
    Ccino::ASTGenerator generator;
    EXPECT_EQ(generator.ProcessClangExpr(nullptr), nullptr);
}

TEST(ProcessClangExprTest, LiteralsAndDeclRefExpr) {
    const auto result = RunProcessClangExprOnCode(R"(
        void TestEntry() {
            int i = 42;
            unsigned int u = 7u;
            float f1 = 3.0f;
            float f2 = 0.25f;
            bool b1 = true;
            bool b2 = false;
            int ref = i;
        }
    )");

    EXPECT_EQ(result.VarInitExprTexts.at("i"), "42");
    EXPECT_EQ(result.VarInitExprTexts.at("u"), "7");
    EXPECT_EQ(result.VarInitExprTexts.at("f1"), "3.0");
    EXPECT_EQ(result.VarInitExprTexts.at("f2"), "0.25");
    EXPECT_EQ(result.VarInitExprTexts.at("b1"), "true");
    EXPECT_EQ(result.VarInitExprTexts.at("b2"), "false");
    EXPECT_EQ(result.VarInitExprTexts.at("ref"), "i");
}

// ============================================================================
// 2. 内置二元、一元、复合赋值与三元条件运算符测试
// ============================================================================

TEST(ProcessClangExprTest, BuiltinBinaryUnaryCompoundAndTernaryOperators) {
    const auto result = RunProcessClangExprOnCode(R"(
        void TestEntry() {
            int a = 1, b = 2, c = 3, d = 4, e = 5;
            bool ok = false;

            a = b + c * d - e / 2 % 3;
            a = b = c;
            ok = a > b && c <= d || e == 5 && a != 0;
            a = b & c | d ^ e << 1 >> 2;

            a += b;
            a -= 1;
            a *= 2 + b;
            a /= 4;
            a %= 3;

            ++a;
            a++;
            --b;
            b--;
            a = -b + ~c + (!ok ? a : d);
        }
    )");

    ASSERT_EQ(result.ExprTexts.size(), 14u);
    EXPECT_EQ(result.ExprTexts[0], "(a = ((b + (c * d)) - ((e / 2) % 3)))");
    EXPECT_EQ(result.EmittedStmts[0], "(a = ((b + (c * d)) - ((e / 2) % 3)));\n");
    EXPECT_EQ(result.ExprTexts[1], "(a = (b = c))");
    EXPECT_EQ(result.ExprTexts[2], "(ok = (((a > b) && (c <= d)) || ((e == 5) && (a != 0))))");
    EXPECT_EQ(result.ExprTexts[3], "(a = ((b & c) | (d ^ ((e << 1) >> 2))))");

    EXPECT_EQ(result.ExprTexts[4], "(a += b)");
    EXPECT_EQ(result.ExprTexts[5], "(a -= 1)");
    EXPECT_EQ(result.ExprTexts[6], "(a *= (2 + b))");
    EXPECT_EQ(result.ExprTexts[7], "(a /= 4)");
    EXPECT_EQ(result.ExprTexts[8], "(a %= 3)");

    EXPECT_EQ(result.ExprTexts[9], "(++a)");
    EXPECT_EQ(result.ExprTexts[10], "(a++)");
    EXPECT_EQ(result.ExprTexts[11], "(--b)");
    EXPECT_EQ(result.ExprTexts[12], "(b--)");
    EXPECT_EQ(result.ExprTexts[13], "(a = (((-b) + (~c)) + ((!ok) ? a : d)))");
}

// ============================================================================
// 3. 重载运算符 (CXXOperatorCallExpr: 向量/矩阵运算、下标、自增自减、前缀一元)
// ============================================================================

TEST(ProcessClangExprTest, OverloadedOperatorsOnVectorsMatricesAndContainers) {
    const auto result = RunProcessClangExprOnCode(kExprTestPreamble + R"(
        void TestEntry() {
            Ccino::IVec3 v1(1, 2, 3);
            Ccino::IVec3 v2(4, 5, 6);
            Ccino::IVec3 v3(7, 8, 9);
            Ccino::Mat4 m1;
            Ccino::Mat4 m2;
            Ccino::Array<int, 4> arr;

            v1 = v2 + v3 * v1 - v2 / v3;
            v1 += v2;
            v1 -= v3;
            v1 *= v2;
            v1 /= v3;
            m1 = m1 * m2;

            ++v1;
            v1++;
            --v2;
            v2--;

            v1 = -v2;
            v1 = ~v3;
            v1 = !v2;

            arr[1] = v1[0] + v2.x;
        }
    )");

    ASSERT_EQ(result.ExprTexts.size(), 14u);
    EXPECT_EQ(result.ExprTexts[0], "(v1 = ((v2 + (v3 * v1)) - (v2 / v3)))");
    EXPECT_EQ(result.ExprTexts[1], "(v1 += v2)");
    EXPECT_EQ(result.ExprTexts[2], "(v1 -= v3)");
    EXPECT_EQ(result.ExprTexts[3], "(v1 *= v2)");
    EXPECT_EQ(result.ExprTexts[4], "(v1 /= v3)");
    EXPECT_EQ(result.ExprTexts[5], "(m1 = (m1 * m2))");

    EXPECT_EQ(result.ExprTexts[6], "(++v1)");
    EXPECT_EQ(result.ExprTexts[7], "(v1++)");
    EXPECT_EQ(result.ExprTexts[8], "(--v2)");
    EXPECT_EQ(result.ExprTexts[9], "(v2--)");

    EXPECT_EQ(result.ExprTexts[10], "(v1 = (-v2))");
    EXPECT_EQ(result.ExprTexts[11], "(v1 = (~v3))");
    EXPECT_EQ(result.ExprTexts[12], "(v1 = (!v2))");

    // 验证匿名 union 穿透 (v2.x) 与 operator[] 下标访问
    EXPECT_EQ(result.ExprTexts[13], "(arr[1] = (v1[0] + v2.x))");
}

// ============================================================================
// 4. 显式类型转换、构造函数、拷贝构造穿透与初始化列表测试
// ============================================================================

TEST(ProcessClangExprTest, CastsConstructorsAndInitLists) {
    const auto result = RunProcessClangExprOnCode(kExprTestPreamble + R"(
        namespace Sim {
            struct Particle {
                Ccino::Vec3 pos;
                int id;
                Particle() = default;
                Particle(Ccino::Vec3 p, int i) : pos(p), id(i) {}
            };

            struct LightSample {
                Ccino::Vec3 radiance;
                float weight;
            };
        }

        void TestEntry() {
            int x = 5;
            float f_cstyle = (float)x;
            float f_func = float(x + 1);
            float f_static = static_cast<float>(x);

            Ccino::Vec3 v_ctor = Ccino::Vec3(1, 2, 3);
            Sim::Particle p1(Ccino::Vec3(1, 2, 3), 7);
            Sim::Particle p2 = p1;

            Sim::LightSample sample = {v_ctor, 0.5f};
            int rawArr[3] = {10, 20, 30};

            rawArr[2] = rawArr[0] + int(f_static);
        }
    )");

    EXPECT_EQ(result.VarInitExprTexts.at("f_cstyle"), "float(x)");
    EXPECT_EQ(result.VarInitExprTexts.at("f_func"), "float((x + 1))");
    EXPECT_EQ(result.VarInitExprTexts.at("f_static"), "float(x)");
    EXPECT_EQ(result.VarInitExprTexts.at("v_ctor"), "vec3(1,2,3)");
    EXPECT_EQ(result.VarInitExprTexts.at("p1"), "Sim_Particle(vec3(1,2,3),7)");
    // 拷贝构造应直接穿透为 p1，不生成非法的 Sim_Particle(p1)
    EXPECT_EQ(result.VarInitExprTexts.at("p2"), "p1");
    EXPECT_EQ(result.VarInitExprTexts.at("sample"), "Sim_LightSample(v_ctor,0.5)");
    EXPECT_EQ(result.VarInitExprTexts.at("rawArr"), "{10,20,30}");

    ASSERT_EQ(result.ExprTexts.size(), 1u);
    EXPECT_EQ(result.ExprTexts[0], "(rawArr[2] = (rawArr[0] + int(f_static)))");
}

// ============================================================================
// 5. 函数调用：GLSL 内建函数 vs 带命名空间/重载签名的全局函数
// ============================================================================

TEST(ProcessClangExprTest, BuiltinFunctionCallsAndFlattenedUserFunctionCalls) {
    const auto result = RunProcessClangExprOnCode(kExprTestPreamble + R"(
        namespace Math {
            float ComputeAttenuation(int level, float weight = 1.0f) {
                return weight;
            }
            float ComputeAttenuation(float dist) {
                return dist;
            }
        }

        void TestEntry() {
            Ccino::Vec3 a(1, 0, 0);
            Ccino::Vec3 b(0, 1, 0);
            int x = 3;
            float w = 0.5f;

            glm::barrier();
            x = glm::clamp(x + 1, 0, 10);
            a = Ccino::Normalize(Ccino::Cross(a, b));
            w = Ccino::Dot(a, b);
            x = Ccino::FloatBitsToInt(w);

            w = Math::ComputeAttenuation(x, w);
            w = Math::ComputeAttenuation(x);
            w = Math::ComputeAttenuation(w);
        }
    )");

    ASSERT_EQ(result.ExprTexts.size(), 8u);
    EXPECT_EQ(result.ExprTexts[0], "barrier()");
    EXPECT_EQ(result.ExprTexts[1], "(x = clamp((x + 1),0,10))");
    EXPECT_EQ(result.ExprTexts[2], "(a = normalize(cross(a,b)))");
    EXPECT_EQ(result.ExprTexts[3], "(w = dot(a,b))");
    EXPECT_EQ(result.ExprTexts[4], "(x = floatBitsToInt(w))");

    // 用户自定义命名空间重载函数应通过 SignatureFlattener 展平作用域与参数类型，默认参数自动补全
    EXPECT_EQ(result.ExprTexts[5], "(w = Math_ComputeAttenuation_int_float(x,w))");
    EXPECT_EQ(result.ExprTexts[6], "(w = Math_ComputeAttenuation_int_float(x,1.0))");
    EXPECT_EQ(result.ExprTexts[7], "(w = Math_ComputeAttenuation_float(w))");
}

// ============================================================================
// 6. 结构体成员函数调用与类内隐式/显式 this 访问集成测试
// ============================================================================

TEST(ProcessClangExprTest, StructMemberCallsAndImplicitExplicitSelfAccess) {
    const std::string code = kExprTestPreamble + R"(
        namespace Physics {
            struct RigidBody {
                Ccino::Vec3 pos;
                Ccino::Vec3 vel;
                float mass;

                void AddImpulse(const Ccino::Vec3& impulse) {
                    vel = vel + impulse;
                }

                void Step(float dt) {
                    // 测试类内隐式调用同类成员函数、显式 this-> 调用、隐式与显式成员变量访问
                    AddImpulse(Ccino::Vec3(0, -9, 0));
                    this->AddImpulse(Ccino::Vec3(1, 0, 0));
                    pos = pos + this->vel * dt;
                }

                float KineticEnergy() const {
                    return 0.5f * mass * Ccino::Dot(vel, vel);
                }

                float Eval(int mode) {
                    return mass;
                }

                float Eval(int mode) const {
                    return mass;
                }
            };
        }

        void TestEntry(Ccino::Accessor<Physics::RigidBody, BufferAccessFlagBits::ReadWrite> bodies) {
            Physics::RigidBody body;
            const Physics::RigidBody constBody{};
            float dt = 0.016f;
            float energy = 0.0f;

            body.Step(dt);
            bodies[0].AddImpulse(Ccino::Vec3(1, 2, 3));
            energy = body.KineticEnergy() + body.Eval(1) + constBody.Eval(1);
        }
    )";

    // 1. 验证外部调用点：obj.Method(args...) -> Namespace_Class_Method_ParamTypes(obj, args...)
    const auto callerResult = RunProcessClangExprOnCode(code, "TestEntry");
    ASSERT_EQ(callerResult.ExprTexts.size(), 3u);
    EXPECT_EQ(callerResult.ExprTexts[0], "Physics_RigidBody_Step_float(body, dt)");
    EXPECT_EQ(callerResult.ExprTexts[1], "Physics_RigidBody_AddImpulse_vec3(bodies[0], vec3(1,2,3))");
    EXPECT_EQ(
        callerResult.ExprTexts[2],
        "(energy = ((Physics_RigidBody_KineticEnergy_const(body) + Physics_RigidBody_Eval_int(body, 1)) + "
        "Physics_RigidBody_Eval_int_const(constBody, 1)))");

    // 2. 验证结构体成员函数内部：m_currentStructDecl 生效，隐式/显式 this 转为 self
    const auto methodResult = RunProcessClangExprOnCode(code, "Step");
    ASSERT_EQ(methodResult.ExprTexts.size(), 3u);
    EXPECT_EQ(methodResult.ExprTexts[0], "Physics_RigidBody_AddImpulse_vec3(self, vec3(0,(-9),0))");
    EXPECT_EQ(methodResult.ExprTexts[1], "Physics_RigidBody_AddImpulse_vec3(self, vec3(1,0,0))");
    EXPECT_EQ(methodResult.ExprTexts[2], "(self.pos = (self.pos + (self.vel * dt)))");
}

// ============================================================================
// 7. 与 GLSLAST 控制流节点 (IfStmt / ForStmt / ReturnStmt / CompoundStmt) 端到端集成测试
// ============================================================================

TEST(ProcessClangExprTest, IntegrationWithGLSLASTStmtEmission) {
    class ExprNodeCollector : public MatchFinder::MatchCallback
    {
    public:
        std::vector<std::unique_ptr<Ccino::GLSL::LoweringStmt>> Lowerings;

        void run(const MatchFinder::MatchResult& Result) override {
            const auto* Func = Result.Nodes.getNodeAs<FunctionDecl>("func");
            if (!Func || !Func->hasBody()) return;

            Ccino::ASTGenerator generator;
            auto* compound = llvm::dyn_cast<CompoundStmt>(Func->getBody());
            if (!compound) return;

            for (Stmt* subStmt : compound->body()) {
                if (auto* expr = llvm::dyn_cast<Expr>(subStmt)) {
                    Lowerings.push_back(generator.ProcessClangExpr(expr));
                }
            }
        }
    };

    ExprNodeCollector collector;
    MatchFinder finder;
    finder.addMatcher(functionDecl(hasName("TestEntry"), isDefinition()).bind("func"), &collector);

    const std::string code = kExprTestPreamble + R"(
        namespace Sim {
            struct Grid {
                int count;
                int NextStep(int delta) { return count + delta; }
            };
        }

        void TestEntry(Sim::Grid g, int i) {
            g.NextStep(i) < 10;
            ++i;
            g.count += i;
            g.NextStep(1);
        }
    )";

    ASSERT_TRUE(
        tooling::runToolOnCodeWithArgs(tooling::newFrontendActionFactory(&finder)->create(), code, {"-std=c++20"}));
    ASSERT_EQ(collector.Lowerings.size(), 4u);

    // 将 ProcessClangExpr 生成的 LoweringStmt 组装进 GLSL::ForStmt + GLSL::ReturnStmt
    auto forStmt = std::make_unique<Ccino::GLSL::ForStmt>();
    auto initDecl = std::make_unique<Ccino::GLSL::DeclStmt>();
    Ccino::GLSL::VarDecl initVar;
    initVar.type = "int";
    initVar.name = "i";
    auto initExpr = std::make_unique<Ccino::GLSL::Expr>();
    initExpr->text = "0";
    initVar.initStmt = std::move(initExpr);
    initDecl->decls.push_back(std::move(initVar));
    forStmt->initStmt = std::move(initDecl);
    forStmt->condStmt = std::move(collector.Lowerings[0]);
    forStmt->incStmt = std::move(collector.Lowerings[1]);

    auto bodyCompound = std::make_unique<Ccino::GLSL::CompoundStmt>();
    bodyCompound->fields.push_back(std::move(collector.Lowerings[2]));
    forStmt->body = std::move(bodyCompound);

    auto retStmt = std::make_unique<Ccino::GLSL::ReturnStmt>();
    retStmt->returnStmt = std::move(collector.Lowerings[3]);

    auto root = std::make_unique<Ccino::GLSL::CompoundStmt>();
    root->fields.push_back(std::move(forStmt));
    root->fields.push_back(std::move(retStmt));

    const std::string expectedGlsl =
        "for (int i = 0;(Sim_Grid_NextStep_int(g, i) < 10);(++i)) {\n"
        "(g.count += i);\n"
        "}\n"
        "return Sim_Grid_NextStep_int(g, 1);\n";

    EXPECT_EQ(root->EmitGLSL(), expectedGlsl);
}

// ============================================================================
// 8. Lambda 捕获变量 (pc.uniformData) 与内建变量转换、嵌套 Lambda 拒绝测试
// ============================================================================

TEST(ProcessClangExprTest, LambdaCapturesToUniformDataAndBuiltinVars) {
    class LambdaBodyExprCollector : public MatchFinder::MatchCallback
    {
    public:
        std::vector<std::string> ExprTexts;
        std::unordered_map<std::string, std::string> VarInitExprTexts;

        void run(const MatchFinder::MatchResult& Result) override {
            const auto* Lambda = Result.Nodes.getNodeAs<LambdaExpr>("lambda");
            if (!Lambda) return;

            Ccino::ASTGenerator generator;
            auto* compound = llvm::dyn_cast<CompoundStmt>(Lambda->getBody());
            if (!compound) return;

            for (Stmt* subStmt : compound->body()) {
                if (auto* declStmt = llvm::dyn_cast<DeclStmt>(subStmt)) {
                    for (Decl* decl : declStmt->decls()) {
                        auto* varDecl = llvm::dyn_cast<VarDecl>(decl);
                        if (!varDecl || !varDecl->hasInit()) continue;
                        auto lowering = generator.ProcessClangExpr(varDecl->getInit());
                        VarInitExprTexts[varDecl->getNameAsString()] =
                            (lowering && lowering->expr) ? lowering->expr->text : std::string{};
                    }
                    continue;
                }
                if (auto* expr = llvm::dyn_cast<Expr>(subStmt)) {
                    auto lowering = generator.ProcessClangExpr(expr);
                    ExprTexts.push_back((lowering && lowering->expr) ? lowering->expr->text : std::string{});
                }
            }
        }
    };

    LambdaBodyExprCollector collector;
    MatchFinder finder;
    finder.addMatcher(lambdaExpr(hasAncestor(functionDecl(hasName("TestEntry")))).bind("lambda"), &collector);

    const std::string code = kExprTestPreamble + R"(
        struct Particle {
            Ccino::Vec3 pos;
            float mass;
        };

        void TestEntry() {
            Ccino::Accessor<Particle, BufferAccessFlagBits::ReadWrite> particles;
            Ccino::Vec3 gravity(0, -9, 0);
            float dt = 0.016f;

            auto cs = [=, scale = 2.5f]() {
                uint32_t idx = Ccino::ComputeGlobalID.x;
                Ccino::UVec3 lid = Ccino::ComputeLocalID;
                uint32_t lidx = Ccino::ComputeLocalIndex;
                // 同名局部变量不应被误替换为 gl_GlobalInvocationID
                int ComputeGlobalID = 10;
                int localCheck = ComputeGlobalID + 1;

                particles[idx].pos = particles[idx].pos + gravity * (dt * scale);
            };
        }
    )";

    ASSERT_TRUE(
        tooling::runToolOnCodeWithArgs(tooling::newFrontendActionFactory(&finder)->create(), code, {"-std=c++20"}));

    EXPECT_EQ(collector.VarInitExprTexts.at("idx"), "gl_GlobalInvocationID.x");
    EXPECT_EQ(collector.VarInitExprTexts.at("lid"), "gl_LocalInvocationID");
    EXPECT_EQ(collector.VarInitExprTexts.at("lidx"), "gl_LocalInvocationIndex");
    EXPECT_EQ(collector.VarInitExprTexts.at("localCheck"), "(ComputeGlobalID + 1)");

    ASSERT_EQ(collector.ExprTexts.size(), 1u);
    EXPECT_EQ(
        collector.ExprTexts[0],
        "(pc.uniformData.particles[idx].pos = "
        "(pc.uniformData.particles[idx].pos + (pc.uniformData.gravity * (pc.uniformData.dt * pc.uniformData.scale))))");
}

TEST(ProcessClangExprTest, UnsupportedNestedLambdaReturnsNull) {
    const auto result = RunProcessClangExprOnCode(R"(
        void TestEntry() {
            auto nested = [](int x) { return x + 1; };
            ([]() {})();
        }
    )");

    EXPECT_EQ(result.VarInitExprTexts.find("nested"), result.VarInitExprTexts.end());
    ASSERT_EQ(result.ExprTexts.size(), 1u);
    EXPECT_EQ(result.ExprTexts[0], "");
}


