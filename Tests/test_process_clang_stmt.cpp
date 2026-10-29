#include <gtest/gtest.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/Stmt.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/Tooling.h>
#include <memory>
#include <string>
#include <vector>

#include "CodeGenerator/ASTGenerator.h"
#include "CodeGenerator/GLSLAST.h"

namespace
{
    using namespace clang;
    using namespace clang::ast_matchers;

    class FunctionBodyStmtCollector : public MatchFinder::MatchCallback
    {
    public:
        std::unique_ptr<Ccino::GLSL::LoweringStmt> BodyLowering;
        std::string BodyGlsl;
        std::vector<std::unique_ptr<Ccino::GLSL::LoweringStmt>> TopLevelLowerings;
        std::vector<std::string> TopLevelStmtGlsls;

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

            Stmt* body = Func->getBody();
            BodyLowering = generator.ProcessClangStmt(body);
            BodyGlsl = BodyLowering ? BodyLowering->EmitGLSL() : std::string{};

            if (auto* compound = llvm::dyn_cast<CompoundStmt>(body)) {
                for (Stmt* subStmt : compound->body()) {
                    auto subLowering = generator.ProcessClangStmt(subStmt);
                    TopLevelStmtGlsls.push_back(subLowering ? subLowering->EmitGLSL() : std::string{});
                    TopLevelLowerings.push_back(std::move(subLowering));
                }
            }
        }
    };

    FunctionBodyStmtCollector RunProcessClangStmtOnCode(const std::string& code,
                                                        const std::string& entryFuncName = "TestEntry") {
        FunctionBodyStmtCollector collector;
        MatchFinder finder;

        finder.addMatcher(functionDecl(hasName(entryFuncName), isDefinition()).bind("func"), &collector);

        EXPECT_TRUE(
            tooling::runToolOnCodeWithArgs(tooling::newFrontendActionFactory(&finder)->create(), code, {"-std=c++20"}));
        return collector;
    }

    const std::string kStmtTestPreamble = R"(
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
        }
    )";
}  // namespace

// ============================================================================
// 1. 边界、空语句与复合语句块测试 (NullPointer / NullStmt / CompoundStmt)
// ============================================================================

TEST(ProcessClangStmtTest, NullPointerReturnsNull) {
    Ccino::ASTGenerator generator;
    EXPECT_EQ(generator.ProcessClangStmt(nullptr), nullptr);
}

TEST(ProcessClangStmtTest, EmptyFunctionBodyAndNullStmt) {
    const auto emptyResult = RunProcessClangStmtOnCode("void TestEntry() {}");
    ASSERT_NE(emptyResult.BodyLowering, nullptr);
    ASSERT_EQ(emptyResult.BodyLowering->fields.size(), 1u);
    EXPECT_EQ(emptyResult.BodyLowering->fields[0]->GetKind(), Ccino::GLSL::Kind::CompoundStmt);
    EXPECT_EQ(emptyResult.BodyGlsl, "");
    EXPECT_TRUE(emptyResult.TopLevelStmtGlsls.empty());

    const auto nullStmtResult = RunProcessClangStmtOnCode(R"(
        void TestEntry() {
            ;
            ;
        }
    )");
    ASSERT_EQ(nullStmtResult.TopLevelStmtGlsls.size(), 2u);
    ASSERT_NE(nullStmtResult.TopLevelLowerings[0], nullptr);
    ASSERT_EQ(nullStmtResult.TopLevelLowerings[0]->fields.size(), 1u);
    EXPECT_EQ(nullStmtResult.TopLevelLowerings[0]->fields[0]->GetKind(), Ccino::GLSL::Kind::NullStmt);
    EXPECT_EQ(nullStmtResult.TopLevelStmtGlsls[0], ";\n");
    EXPECT_EQ(nullStmtResult.TopLevelStmtGlsls[1], ";\n");
    EXPECT_EQ(nullStmtResult.BodyGlsl, ";\n;\n");
}

TEST(ProcessClangStmtTest, NestedCompoundStmt) {
    const auto result = RunProcessClangStmtOnCode(R"(
        void TestEntry() {
            int a = 1;
            {
                int b = 2;
                {
                    a = a + b;
                }
            }
        }
    )");

    ASSERT_EQ(result.TopLevelStmtGlsls.size(), 2u);
    EXPECT_EQ(result.TopLevelStmtGlsls[0], "int a = 1;\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[1], "int b = 2;\n(a = (a + b));\n");
    EXPECT_EQ(result.BodyGlsl, "int a = 1;\nint b = 2;\n(a = (a + b));\n");
}

// ============================================================================
// 2. 变量声明语句 (DeclStmt)
// ============================================================================

TEST(ProcessClangStmtTest, DeclStmt_ScalarsMultiDeclAndAuto) {
    const auto result = RunProcessClangStmtOnCode(kStmtTestPreamble + R"(
        void TestEntry() {
            int a;
            int x = 1, y = 2, z;
            uint32_t u = 10u;
            bool flag = true;
            const int& ref_x = x;
            auto sum = x + y * 3;
        }
    )");

    ASSERT_EQ(result.TopLevelStmtGlsls.size(), 6u);
    ASSERT_NE(result.TopLevelLowerings[1], nullptr);
    ASSERT_EQ(result.TopLevelLowerings[1]->fields.size(), 1u);
    ASSERT_EQ(result.TopLevelLowerings[1]->fields[0]->GetKind(), Ccino::GLSL::Kind::DeclStmt);
    const auto* multiDecl = static_cast<const Ccino::GLSL::DeclStmt*>(result.TopLevelLowerings[1]->fields[0].get());
    ASSERT_EQ(multiDecl->decls.size(), 3u);
    EXPECT_EQ(multiDecl->decls[0].type, "int");
    EXPECT_EQ(multiDecl->decls[0].name, "x");
    EXPECT_NE(multiDecl->decls[0].initStmt, nullptr);
    EXPECT_EQ(multiDecl->decls[2].type, "int");
    EXPECT_EQ(multiDecl->decls[2].name, "z");
    EXPECT_EQ(multiDecl->decls[2].initStmt, nullptr);

    EXPECT_EQ(result.TopLevelStmtGlsls[0], "int a;\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[1], "int x = 1;\nint y = 2;\nint z;\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[2], "uint u = 10;\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[3], "bool flag = true;\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[4], "int ref_x = x;\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[5], "int sum = (x + (y * 3));\n");
}

TEST(ProcessClangStmtTest, DeclStmt_VectorsArraysStructsAndInvalidAccessor) {
    const auto result = RunProcessClangStmtOnCode(kStmtTestPreamble + R"(
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
            Ccino::Vec3 uninitVec;
            Ccino::IVec3 iv(1, 2, 3);
            int cArr[3] = {10, 20, 30};
            float matArr[2][4];
            Ccino::Array<int, 2> stdArr;
            Sim::Particle pDefault;
            const Sim::Particle pBraceEmpty{};
            Sim::Particle p1(Ccino::Vec3(1, 2, 3), 7);
            Sim::Particle p2 = p1;
            Sim::LightSample sample = {uninitVec, 0.5f};
            Ccino::Accessor<Sim::Particle, BufferAccessFlagBits::ReadWrite> illegalLocalBuf;
        }
    )");

    ASSERT_EQ(result.TopLevelStmtGlsls.size(), 11u);
    EXPECT_EQ(result.TopLevelStmtGlsls[0], "vec3 uninitVec;\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[1], "ivec3 iv = ivec3(1,2,3);\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[2], "int cArr[3] = {10,20,30};\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[3], "float matArr[2][4];\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[4], "int stdArr[2];\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[5], "Sim_Particle pDefault;\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[6], "Sim_Particle pBraceEmpty;\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[7], "Sim_Particle p1 = Sim_Particle(vec3(1,2,3),7);\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[8], "Sim_Particle p2 = p1;\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[9], "Sim_LightSample sample = Sim_LightSample(uninitVec,0.5);\n");
    // 函数体内不允许声明 Accessor，应返回 nullptr
    EXPECT_EQ(result.TopLevelLowerings[10], nullptr);
    EXPECT_EQ(result.TopLevelStmtGlsls[10], "");
}

TEST(ProcessClangStmtTest, DeclStmt_UnsupportedNonVarDeclAndNestedLambdaReturnNull) {
    const auto result = RunProcessClangStmtOnCode(R"(
        void TestEntry() {
            using LocalInt = int;
            auto nested = [](int x) { return x + 1; };
        }
    )");

    ASSERT_EQ(result.TopLevelLowerings.size(), 2u);
    EXPECT_EQ(result.TopLevelLowerings[0], nullptr);
    EXPECT_EQ(result.TopLevelLowerings[1], nullptr);
}

// ============================================================================
// 3. Return 语句 (ReturnStmt)
// ============================================================================

TEST(ProcessClangStmtTest, ReturnStmt_VoidAndValueReturns) {
    const auto voidReturn = RunProcessClangStmtOnCode(R"(
        void TestEntry() {
            return;
        }
    )");
    ASSERT_EQ(voidReturn.TopLevelLowerings.size(), 1u);
    ASSERT_NE(voidReturn.TopLevelLowerings[0], nullptr);
    ASSERT_EQ(voidReturn.TopLevelLowerings[0]->fields.size(), 1u);
    EXPECT_EQ(voidReturn.TopLevelLowerings[0]->fields[0]->GetKind(), Ccino::GLSL::Kind::ReturnStmt);
    EXPECT_EQ(voidReturn.TopLevelStmtGlsls[0], "return;\n");

    const auto exprReturn = RunProcessClangStmtOnCode(kStmtTestPreamble + R"(
        int TestEntry() {
            int a = 2, b = 3;
            return Ccino::Clamp(a + b * 4, 0, 100);
        }
    )");
    ASSERT_EQ(exprReturn.TopLevelStmtGlsls.size(), 2u);
    EXPECT_EQ(exprReturn.TopLevelStmtGlsls[1], "return clamp((a + (b * 4)),0,100);\n");
}

// ============================================================================
// 4. 条件分支语句 (IfStmt)
// ============================================================================

TEST(ProcessClangStmtTest, IfStmt_SimpleIfAndSingleStatementThen) {
    const auto result = RunProcessClangStmtOnCode(R"(
        void TestEntry() {
            int a = 1, b = 0;
            if (a > 0) {
                b = 10;
            }
            if (a == 1)
                b = 20;
        }
    )");

    ASSERT_EQ(result.TopLevelStmtGlsls.size(), 3u);
    ASSERT_NE(result.TopLevelLowerings[1], nullptr);
    ASSERT_EQ(result.TopLevelLowerings[1]->fields.size(), 1u);
    EXPECT_EQ(result.TopLevelLowerings[1]->fields[0]->GetKind(), Ccino::GLSL::Kind::IfStmt);

    EXPECT_EQ(
        result.TopLevelStmtGlsls[1],
        "if ((a > 0)) {\n"
        "(b = 10);\n"
        "}\n");
    EXPECT_EQ(
        result.TopLevelStmtGlsls[2],
        "if ((a == 1)) {\n"
        "(b = 20);\n"
        "}\n");
}

TEST(ProcessClangStmtTest, IfStmt_IfElseAndElseIfChainAndNestedIf) {
    const auto result = RunProcessClangStmtOnCode(R"(
        void TestEntry() {
            int a = 1, b = 0;

            if (a > 0) {
                b = 1;
            } else {
                b = 2;
            }

            if (a > 10) {
                if (b == 1) {
                    b = 100;
                }
            } else if (a > 0) {
                b = 50;
            } else {
                b = 0;
            }
        }
    )");

    ASSERT_EQ(result.TopLevelStmtGlsls.size(), 3u);
    EXPECT_EQ(
        result.TopLevelStmtGlsls[1],
        "if ((a > 0)) {\n"
        "(b = 1);\n"
        "}\n"
        "else {\n"
        "(b = 2);\n"
        "}\n");

    EXPECT_EQ(
        result.TopLevelStmtGlsls[2],
        "if ((a > 10)) {\n"
        "if ((b == 1)) {\n"
        "(b = 100);\n"
        "}\n"
        "}\n"
        "else {\n"
        "if ((a > 0)) {\n"
        "(b = 50);\n"
        "}\n"
        "else {\n"
        "(b = 0);\n"
        "}\n"
        "}\n");
}

TEST(ProcessClangStmtTest, IfStmt_UnsupportedInitAndConditionVarReturnNull) {
    const auto result = RunProcessClangStmtOnCode(R"(
        void TestEntry() {
            int a = 1;
            if (int x = 1; x > 0) {
                x = 2;
            }
            if (int y = a) {
                a = 0;
            }
        }
    )");

    ASSERT_EQ(result.TopLevelLowerings.size(), 3u);
    EXPECT_EQ(result.TopLevelLowerings[1], nullptr);
    EXPECT_EQ(result.TopLevelLowerings[2], nullptr);
}

// ============================================================================
// 5. While 循环语句与 Break / Continue (WhileStmt / BreakStmt / ContinueStmt)
// ============================================================================

TEST(ProcessClangStmtTest, WhileStmt_WithBreakAndContinue) {
    const auto result = RunProcessClangStmtOnCode(R"(
        void TestEntry() {
            int i = 0;
            int sum = 0;
            while (i < 10) {
                i = i + 1;
                if (i == 3) {
                    continue;
                }
                if (i == 8) {
                    break;
                }
                sum += i;
            }
        }
    )");

    ASSERT_EQ(result.TopLevelStmtGlsls.size(), 3u);
    ASSERT_NE(result.TopLevelLowerings[2], nullptr);
    ASSERT_EQ(result.TopLevelLowerings[2]->fields.size(), 1u);
    EXPECT_EQ(result.TopLevelLowerings[2]->fields[0]->GetKind(), Ccino::GLSL::Kind::WhileStmt);

    EXPECT_EQ(
        result.TopLevelStmtGlsls[2],
        "while ((i < 10)) {\n"
        "(i = (i + 1));\n"
        "if ((i == 3)) {\n"
        "continue;\n"
        "}\n"
        "if ((i == 8)) {\n"
        "break;\n"
        "}\n"
        "(sum += i);\n"
        "}\n");
}

TEST(ProcessClangStmtTest, WhileStmt_ConditionVariableDeclReturnsNull) {
    const auto result = RunProcessClangStmtOnCode(R"(
        void TestEntry() {
            int a = 1;
            while (int x = a) {
                a = 0;
            }
        }
    )");

    ASSERT_EQ(result.TopLevelLowerings.size(), 2u);
    EXPECT_EQ(result.TopLevelLowerings[1], nullptr);
}

// ============================================================================
// 6. For 循环语句 (ForStmt)
// ============================================================================

TEST(ProcessClangStmtTest, ForStmt_StandardAndOmittedClausesAndNested) {
    const auto result = RunProcessClangStmtOnCode(R"(
        void TestEntry() {
            int sum = 0;

            for (int i = 0; i < 10; ++i) {
                sum += i;
            }

            int j = 0;
            for (; j < 5; j++) {
                sum += j;
            }

            for (j = 1; j < 4; ++j) {
                sum += j;
            }

            for (;;) {
                break;
            }

            for (int r = 0; r < 4; r += 1) {
                for (int c = 0; c < 4; c += 1) {
                    sum = sum + r * 4 + c;
                }
            }

            for (int a = 0, b = 10; a < b; ++a) {
                sum += a;
            }
        }
    )");

    ASSERT_EQ(result.TopLevelStmtGlsls.size(), 8u);
    ASSERT_NE(result.TopLevelLowerings[1], nullptr);
    ASSERT_EQ(result.TopLevelLowerings[1]->fields.size(), 1u);
    EXPECT_EQ(result.TopLevelLowerings[1]->fields[0]->GetKind(), Ccino::GLSL::Kind::ForStmt);

    EXPECT_EQ(
        result.TopLevelStmtGlsls[1],
        "for (int i = 0;(i < 10);(++i)) {\n"
        "(sum += i);\n"
        "}\n");
    EXPECT_EQ(
        result.TopLevelStmtGlsls[3],
        "for (;(j < 5);(j++)) {\n"
        "(sum += j);\n"
        "}\n");
    EXPECT_EQ(
        result.TopLevelStmtGlsls[4],
        "for ((j = 1);(j < 4);(++j)) {\n"
        "(sum += j);\n"
        "}\n");
    EXPECT_EQ(
        result.TopLevelStmtGlsls[5],
        "for (;;) {\n"
        "break;\n"
        "}\n");
    EXPECT_EQ(
        result.TopLevelStmtGlsls[6],
        "for (int r = 0;(r < 4);(r += 1)) {\n"
        "for (int c = 0;(c < 4);(c += 1)) {\n"
        "(sum = ((sum + (r * 4)) + c));\n"
        "}\n"
        "}\n");
    EXPECT_EQ(
        result.TopLevelStmtGlsls[7],
        "int a = 0;\n"
        "for (int b = 10;(a < b);(++a)) {\n"
        "(sum += a);\n"
        "}\n");
}

TEST(ProcessClangStmtTest, ForStmt_ConditionVariableDeclReturnsNull) {
    const auto result = RunProcessClangStmtOnCode(R"(
        void TestEntry() {
            for (int i = 0; int condVar = i; i = i + 1) {
                i = 0;
            }
        }
    )");

    ASSERT_EQ(result.TopLevelLowerings.size(), 1u);
    EXPECT_EQ(result.TopLevelLowerings[0], nullptr);
}

// ============================================================================
// 7. 不支持的语句类型兜底测试 (Range-For / Do-While / Switch)
// ============================================================================

TEST(ProcessClangStmtTest, UnsupportedStatementsReturnNull) {
    const auto result = RunProcessClangStmtOnCode(R"(
        void TestEntry() {
            int arr[3] = {1, 2, 3};
            int x = 0;

            for (int v : arr) {
                x += v;
            }

            do {
                x += 1;
            } while (x < 5);

            switch (x) {
                case 1: x = 2; break;
                default: break;
            }
        }
    )");

    ASSERT_EQ(result.TopLevelLowerings.size(), 5u);
    EXPECT_NE(result.TopLevelLowerings[0], nullptr);
    EXPECT_NE(result.TopLevelLowerings[1], nullptr);
    EXPECT_EQ(result.TopLevelLowerings[2], nullptr);  // CXXForRangeStmt
    EXPECT_EQ(result.TopLevelLowerings[3], nullptr);  // DoStmt
    EXPECT_EQ(result.TopLevelLowerings[4], nullptr);  // SwitchStmt
}

// ============================================================================
// 8. 综合复杂函数体与结构体成员函数体测试 (Full Shader Kernel & Struct Method)
// ============================================================================

TEST(ProcessClangStmtTest, ComprehensiveShaderKernelAndStructMethodBody) {
    const std::string code = kStmtTestPreamble + R"(
        namespace Physics {
            struct Body {
                Ccino::Vec3 pos;
                Ccino::Vec3 vel;
                int active;

                void Integrate(float dt, int subSteps) {
                    if (active == 0) {
                        return;
                    }
                    for (int s = 0; s < subSteps; ++s) {
                        pos = pos + this->vel * dt;
                    }
                }
            };
        }

        int TestEntry(Ccino::Accessor<Physics::Body, BufferAccessFlagBits::ReadWrite> bodies) {
            Ccino::Vec3 pos(1, 2, 3);
            Ccino::Vec3 vel(4, 5, 6);
            int acc = 0;

            for (int step = 0; step < 8; step += 1) {
                pos = pos + vel;
                if (step == 2) {
                    continue;
                } else if (step > 6) {
                    break;
                } else {
                    acc += step * 2;
                }
            }

            while (acc > 10) {
                acc -= 3;
            }

            bodies[0].Integrate(0.25f, 4);
            glm::barrier();
            return Ccino::Clamp(acc, 0, 255);
        }
    )";

    // 1. 验证全局入口函数体转换
    const auto entryResult = RunProcessClangStmtOnCode(code, "TestEntry");
    const std::string expectedEntryBody =
        "vec3 pos = vec3(1,2,3);\n"
        "vec3 vel = vec3(4,5,6);\n"
        "int acc = 0;\n"
        "for (int step = 0;(step < 8);(step += 1)) {\n"
        "(pos = (pos + vel));\n"
        "if ((step == 2)) {\n"
        "continue;\n"
        "}\n"
        "else {\n"
        "if ((step > 6)) {\n"
        "break;\n"
        "}\n"
        "else {\n"
        "(acc += (step * 2));\n"
        "}\n"
        "}\n"
        "}\n"
        "while ((acc > 10)) {\n"
        "(acc -= 3);\n"
        "}\n"
        "Physics_Body_Integrate_float_int(bodies[0], 0.25, 4);\n"
        "barrier();\n"
        "return clamp(acc,0,255);\n";
    EXPECT_EQ(entryResult.BodyGlsl, expectedEntryBody);

    // 2. 验证结构体成员函数体转换 (SetCurrentStructDecl 生效，隐式/显式成员转 self.<member>)
    const auto methodResult = RunProcessClangStmtOnCode(code, "Integrate");
    const std::string expectedMethodBody =
        "if ((self.active == 0)) {\n"
        "return;\n"
        "}\n"
        "for (int s = 0;(s < subSteps);(++s)) {\n"
        "(self.pos = (self.pos + (self.vel * dt)));\n"
        "}\n";
    EXPECT_EQ(methodResult.BodyGlsl, expectedMethodBody);
}
