#include <gtest/gtest.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/Tooling.h>
#include <string>
#include <vector>

#include "CodeGenerator/CppParser.h"

namespace
{
    using namespace clang;
    using namespace clang::ast_matchers;

    // 收集 TestEntry 函数体：
    // 1. BodyGlsl: 直接对整个函数体的 CompoundStmt 调用 ProcessSTMT 的结果
    // 2. TopLevelStmtGlsls: 对函数体内每一条顶层子语句分别调用 ProcessSTMT 的结果列表
    class FunctionBodyStmtCollector : public MatchFinder::MatchCallback
    {
    public:
        std::string BodyGlsl;
        std::string FuncGlsl;
        std::vector<std::string> TopLevelStmtGlsls;

        void run(const MatchFinder::MatchResult& Result) override {
            const auto* Func = Result.Nodes.getNodeAs<FunctionDecl>("func");
            if (!Func || !Func->hasBody()) return;

            Stmt* body = Func->getBody();
            BodyGlsl = Ccino::CppParser::ProcessSTMT(body);
            FuncGlsl = Ccino::CppParser::GenerateGlobalFunctionToGLSL(const_cast<FunctionDecl*>(Func));

            if (auto* compound = llvm::dyn_cast<CompoundStmt>(body)) {
                for (Stmt* subStmt : compound->body()) {
                    TopLevelStmtGlsls.push_back(Ccino::CppParser::ProcessSTMT(subStmt));
                }
            }
        }
    };

    FunctionBodyStmtCollector RunProcessStmtOnCode(const std::string& code) {
        FunctionBodyStmtCollector collector;
        MatchFinder finder;

        finder.addMatcher(functionDecl(hasName("TestEntry"), isDefinition()).bind("func"), &collector);

        EXPECT_TRUE(
            tooling::runToolOnCodeWithArgs(tooling::newFrontendActionFactory(&finder)->create(), code, {"-std=c++20"}));
        return collector;
    }

    // 常用前置类型模拟（Vec / Mat / Array / Accessor / 数学函数）
    const std::string kShaderTestPreamble = R"(
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
            };
        }

        namespace glm {
            enum qualifier { defaultp };

            template<int L, typename T, qualifier Q = defaultp>
            struct vec {
                vec() = default;
                vec(T s) {}
                vec(T x, T y) {}
                vec(T x, T y, T z) {}
                vec(T x, T y, T z, T w) {}

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

            template<typename T, unsigned long long N>
            using Array = std::array<T, N>;

            template<typename BufferResourceType, BufferAccessFlags bufferAccessFlags>
            class Accessor {};
        }
    )";
}  // namespace

// ============================================================================
// 1. 边界与空语句测试 (Null / Empty / Jump Statements)
// ============================================================================

TEST(ProcessStmtTest, NullPointerReturnsEmpty) {
    EXPECT_EQ(Ccino::CppParser::ProcessSTMT(nullptr), "");
}

TEST(ProcessStmtTest, EmptyFunctionBodyAndNullStmt) {
    const auto emptyResult = RunProcessStmtOnCode("void TestEntry() {}");
    EXPECT_EQ(emptyResult.BodyGlsl, "");
    EXPECT_TRUE(emptyResult.TopLevelStmtGlsls.empty());

    const auto nullStmtResult = RunProcessStmtOnCode(R"(
        void TestEntry() {
            ;
            ;
        }
    )");
    ASSERT_EQ(nullStmtResult.TopLevelStmtGlsls.size(), 2u);
    EXPECT_EQ(nullStmtResult.TopLevelStmtGlsls[0], ";\n");
    EXPECT_EQ(nullStmtResult.TopLevelStmtGlsls[1], ";\n");
    EXPECT_EQ(nullStmtResult.BodyGlsl, ";\n;\n");
}

TEST(ProcessStmtTest, NestedCompoundStmt) {
    const auto result = RunProcessStmtOnCode(R"(
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

TEST(ProcessStmtTest, DeclStmt_ScalarsMultiDeclAndAuto) {
    const auto result = RunProcessStmtOnCode(kShaderTestPreamble + R"(
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
    EXPECT_EQ(result.TopLevelStmtGlsls[0], "int a;\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[1], "int x = 1;\nint y = 2;\nint z;\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[2], "uint u = 10;\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[3], "bool flag = true;\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[4], "int ref_x = x;\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[5], "int sum = (x + (y * 3));\n");
}

TEST(ProcessStmtTest, DeclStmt_VectorsArraysStructsAndAccessor) {
    const auto result = RunProcessStmtOnCode(kShaderTestPreamble + R"(
        struct Particle {
            Ccino::Vec3 pos;
            int id;
            Particle() = default;
            Particle(Ccino::Vec3 p, int i) : pos(p), id(i) {}
        };

        void TestEntry() {
            Ccino::IVec3 iv(1, 2, 3);
            int cArr[3] = {10, 20, 30};
            Ccino::Array<int, 2> stdArr;
            Particle p(Ccino::Vec3(1, 2, 3), 7);
            Ccino::Accessor<Particle, BufferAccessFlagBits::ReadWrite> particleBuf;
        }
    )");

    ASSERT_EQ(result.TopLevelStmtGlsls.size(), 5u);
    EXPECT_EQ(result.TopLevelStmtGlsls[0], "ivec3 iv = ivec3(1, 2, 3);\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[1], "int cArr[3] = {10, 20, 30};\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[2], "int stdArr[2] = int[2]();\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[3], "Particle p = Particle(vec3(1,2,3), 7);\n");
    // 函数体内不允许声明 Accessor，应报错并返回空字符串
    EXPECT_EQ(result.TopLevelStmtGlsls[4], "");
}

TEST(ProcessStmtTest, DeclStmt_UnsupportedNonVarDeclReturnsEmpty) {
    const auto result = RunProcessStmtOnCode(R"(
        void TestEntry() {
            using LocalInt = int;
        }
    )");

    ASSERT_EQ(result.TopLevelStmtGlsls.size(), 1u);
    EXPECT_EQ(result.TopLevelStmtGlsls[0], "");
}

// ============================================================================
// 3. 全表达式语句 (Expr Stmt: 赋值、嵌套算术/逻辑/位运算、重载运算符、函数调用)
// ============================================================================

TEST(ProcessStmtTest, ExprStmt_BuiltinBinaryAndNestedExpressions) {
    const auto result = RunProcessStmtOnCode(R"(
        void TestEntry() {
            int a = 1, b = 2, c = 3, d = 4, e = 5;
            bool ok = false;

            a = b + c;
            a = b + c * d - e / 2 % 3;
            a = b = c;
            ok = a > b && c <= d || e == 5 && a != 0;
            a = b & c | d ^ e << 1 >> 2;
        }
    )");

    ASSERT_EQ(result.TopLevelStmtGlsls.size(), 7u);
    EXPECT_EQ(result.TopLevelStmtGlsls[2], "(a = (b + c));\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[3], "(a = ((b + (c * d)) - ((e / 2) % 3)));\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[4], "(a = (b = c));\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[5], "(ok = (((a > b) && (c <= d)) || ((e == 5) && (a != 0))));\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[6], "(a = ((b & c) | (d ^ ((e << 1) >> 2))));\n");
}

TEST(ProcessStmtTest, ExprStmt_BuiltinCompoundAssignments) {
    const auto result = RunProcessStmtOnCode(R"(
        void TestEntry() {
            int a = 10, b = 3;
            a += b;
            a -= 1;
            a *= 2 + b;
            a /= 4;
            a %= 3;
            a &= 7;
            a |= 1;
            a ^= 2;
            a <<= 1;
            a >>= 2;
        }
    )");

    ASSERT_EQ(result.TopLevelStmtGlsls.size(), 11u);
    EXPECT_EQ(result.TopLevelStmtGlsls[1], "(a += b);\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[2], "(a -= 1);\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[3], "(a *= (2 + b));\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[4], "(a /= 4);\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[5], "(a %= 3);\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[6], "(a &= 7);\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[7], "(a |= 1);\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[8], "(a ^= 2);\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[9], "(a <<= 1);\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[10], "(a >>= 2);\n");
}

TEST(ProcessStmtTest, ExprStmt_OverloadedOperatorsOnVectorsAndMatrices) {
    const auto result = RunProcessStmtOnCode(kShaderTestPreamble + R"(
        void TestEntry() {
            Ccino::IVec3 v1(1, 2, 3);
            Ccino::IVec3 v2(4, 5, 6);
            Ccino::IVec3 v3(7, 8, 9);
            Ccino::Mat4 m1;
            Ccino::Mat4 m2;

            // 中缀二元重载运算符与嵌套
            v1 = v2 + v3 * v1 - v2 / v3;
            v1 += v2;
            v1 -= v3;
            v1 *= v2;
            v1 /= v3;
            m1 = m1 * m2;

            // 前缀与后缀 ++ / -- 重载运算符
            ++v1;
            v1++;
            --v2;
            v2--;

            // 前缀一元重载运算符 (-, ~, !)
            v1 = -v2;
            v1 = ~v3;
            v1 = !v2;
        }
    )");

    ASSERT_EQ(result.TopLevelStmtGlsls.size(), 18u);
    EXPECT_EQ(result.TopLevelStmtGlsls[5], "(v1 = ((v2 + (v3 * v1)) - (v2 / v3)));\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[6], "(v1 += v2);\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[7], "(v1 -= v3);\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[8], "(v1 *= v2);\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[9], "(v1 /= v3);\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[10], "(m1 = (m1 * m2));\n");

    EXPECT_EQ(result.TopLevelStmtGlsls[11], "(++v1);\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[12], "(v1++);\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[13], "(--v2);\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[14], "(v2--);\n");

    EXPECT_EQ(result.TopLevelStmtGlsls[15], "(v1 = (-v2));\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[16], "(v1 = (~v3));\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[17], "(v1 = (!v2));\n");
}

TEST(ProcessStmtTest, ExprStmt_FunctionCallsAndConstructors) {
    const auto result = RunProcessStmtOnCode(kShaderTestPreamble + R"(
        void TestEntry() {
            Ccino::Vec3 a;
            Ccino::Vec3 b;
            int x = 5;

            glm::barrier();
            x = glm::clamp(x + 1, 0, 10);
            a = glm::normalize(glm::cross(a, b));
            b = Ccino::Vec3(1, 2, 3) + a;
        }
    )");

    ASSERT_EQ(result.TopLevelStmtGlsls.size(), 7u);
    EXPECT_EQ(result.TopLevelStmtGlsls[3], "barrier();\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[4], "(x = clamp((x + 1), 0, 10));\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[5], "(a = normalize(cross(a, b)));\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[6], "(b = (vec3(1,2,3) + a));\n");
}

// ============================================================================
// 4. Return 语句 (ReturnStmt)
// ============================================================================

TEST(ProcessStmtTest, ReturnStmt_VoidAndValueReturns) {
    const auto voidReturn = RunProcessStmtOnCode(R"(
        void TestEntry() {
            return;
        }
    )");
    ASSERT_EQ(voidReturn.TopLevelStmtGlsls.size(), 1u);
    EXPECT_EQ(voidReturn.TopLevelStmtGlsls[0], "return ;\n");

    const auto exprReturn = RunProcessStmtOnCode(kShaderTestPreamble + R"(
        int TestEntry() {
            int a = 2, b = 3;
            return glm::clamp(a + b * 4, 0, 100);
        }
    )");
    ASSERT_EQ(exprReturn.TopLevelStmtGlsls.size(), 2u);
    EXPECT_EQ(exprReturn.TopLevelStmtGlsls[1], "return clamp((a + (b * 4)), 0, 100);\n");
}

// ============================================================================
// 5. 条件分支语句 (IfStmt)
// ============================================================================

TEST(ProcessStmtTest, IfStmt_SimpleIfAndSingleStatementThen) {
    const auto result = RunProcessStmtOnCode(R"(
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
    EXPECT_EQ(
        result.TopLevelStmtGlsls[1],
        "if ((a > 0)) \n"
        "{ \n"
        "(b = 10);\n"
        "}\n");
    EXPECT_EQ(
        result.TopLevelStmtGlsls[2],
        "if ((a == 1)) \n"
        "{ \n"
        "(b = 20);\n"
        "}\n");
}

TEST(ProcessStmtTest, IfStmt_IfElseAndElseIfChainAndNestedIf) {
    const auto result = RunProcessStmtOnCode(R"(
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
        "if ((a > 0)) \n"
        "{ \n"
        "(b = 1);\n"
        "}\n"
        "else {\n"
        "(b = 2);\n"
        "}\n");

    EXPECT_EQ(
        result.TopLevelStmtGlsls[2],
        "if ((a > 10)) \n"
        "{ \n"
        "if ((b == 1)) \n"
        "{ \n"
        "(b = 100);\n"
        "}\n"
        "}\n"
        "else {\n"
        "if ((a > 0)) \n"
        "{ \n"
        "(b = 50);\n"
        "}\n"
        "else {\n"
        "(b = 0);\n"
        "}\n"
        "}\n");
}

TEST(ProcessStmtTest, IfStmt_Cpp17InitStatementReturnsEmpty) {
    const auto result = RunProcessStmtOnCode(R"(
        void TestEntry() {
            if (int x = 1; x > 0) {
                x = 2;
            }
        }
    )");

    ASSERT_EQ(result.TopLevelStmtGlsls.size(), 1u);
    EXPECT_EQ(result.TopLevelStmtGlsls[0], "");
}

// ============================================================================
// 6. While 循环语句与 Break / Continue (WhileStmt / BreakStmt / ContinueStmt)
// ============================================================================

TEST(ProcessStmtTest, WhileStmt_WithBreakAndContinue) {
    const auto result = RunProcessStmtOnCode(R"(
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
    EXPECT_EQ(
        result.TopLevelStmtGlsls[2],
        "while ((i < 10)) {\n"
        "(i = (i + 1));\n"
        "if ((i == 3)) \n"
        "{ \n"
        "continue;\n"
        "}\n"
        "if ((i == 8)) \n"
        "{ \n"
        "break;\n"
        "}\n"
        "(sum += i);\n"
        "}");
}

TEST(ProcessStmtTest, WhileStmt_ConditionVariableDeclReturnsEmpty) {
    const auto result = RunProcessStmtOnCode(R"(
        void TestEntry() {
            int a = 1;
            while (int x = a) {
                a = 0;
            }
        }
    )");

    ASSERT_EQ(result.TopLevelStmtGlsls.size(), 2u);
    EXPECT_EQ(result.TopLevelStmtGlsls[1], "");
}

// ============================================================================
// 7. For 循环语句 (ForStmt)
// ============================================================================

TEST(ProcessStmtTest, ForStmt_StandardAndOmittedClausesAndNested) {
    const auto result = RunProcessStmtOnCode(R"(
        void TestEntry() {
            int sum = 0;

            for (int i = 0; i < 10; ++i) {
                sum += i;
            }

            int j = 0;
            for (; j < 5; j++) {
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
        }
    )");

    ASSERT_EQ(result.TopLevelStmtGlsls.size(), 6u);
    EXPECT_EQ(
        result.TopLevelStmtGlsls[1],
        "for (int i = 0;\n(i < 10);(++i)) { \n"
        "(sum += i);\n"
        "}");
    EXPECT_EQ(
        result.TopLevelStmtGlsls[3],
        "for (;(j < 5);(j++)) { \n"
        "(sum += j);\n"
        "}");
    EXPECT_EQ(
        result.TopLevelStmtGlsls[4],
        "for (;;) { \n"
        "break;\n"
        "}");
    EXPECT_EQ(
        result.TopLevelStmtGlsls[5],
        "for (int r = 0;\n(r < 4);(r += 1)) { \n"
        "for (int c = 0;\n(c < 4);(c += 1)) { \n"
        "(sum = ((sum + (r * 4)) + c));\n"
        "}"
        "}");
}

TEST(ProcessStmtTest, ForStmt_ConditionVariableDeclReturnsEmpty) {
    const auto result = RunProcessStmtOnCode(R"(
        void TestEntry() {
            for (int i = 0; int condVar = i; i = i + 1) {
                i = 0;
            }
        }
    )");

    ASSERT_EQ(result.TopLevelStmtGlsls.size(), 1u);
    EXPECT_EQ(result.TopLevelStmtGlsls[0], "");
}

// ============================================================================
// 8. 不支持的语句类型兜底测试 (Range-For / Switch / Do-While)
// ============================================================================

TEST(ProcessStmtTest, UnsupportedStatementsReturnEmpty) {
    const auto result = RunProcessStmtOnCode(R"(
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

    ASSERT_EQ(result.TopLevelStmtGlsls.size(), 5u);
    EXPECT_EQ(result.TopLevelStmtGlsls[2], "");  // CXXForRangeStmt
    EXPECT_EQ(result.TopLevelStmtGlsls[3], "");  // DoStmt
    EXPECT_EQ(result.TopLevelStmtGlsls[4], "");  // SwitchStmt
}

// ============================================================================
// 9. 综合复杂函数体测试 (Full Shader Kernel Body Simulation)
// ============================================================================

TEST(ProcessStmtTest, ComprehensiveShaderKernelBody) {
    const auto result = RunProcessStmtOnCode(kShaderTestPreamble + R"(
        int TestEntry() {
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

            glm::barrier();
            return glm::clamp(acc, 0, 255);
        }
    )");

    const std::string expectedBody =
        "vec3 pos = vec3(1, 2, 3);\n"
        "vec3 vel = vec3(4, 5, 6);\n"
        "int acc = 0;\n"
        "for (int step = 0;\n(step < 8);(step += 1)) { \n"
        "(pos = (pos + vel));\n"
        "if ((step == 2)) \n"
        "{ \n"
        "continue;\n"
        "}\n"
        "else {\n"
        "if ((step > 6)) \n"
        "{ \n"
        "break;\n"
        "}\n"
        "else {\n"
        "(acc += (step * 2));\n"
        "}\n"
        "}\n"
        "}"
        "while ((acc > 10)) {\n"
        "(acc -= 3);\n"
        "}"
        "barrier();\n"
        "return clamp(acc, 0, 255);\n";

    EXPECT_EQ(result.BodyGlsl, expectedBody);
}

// ============================================================================
// 10. 全局函数转 GLSL 测试 (GenerateGlobalFunctionToGLSL)
// ============================================================================

TEST(ProcessStmtTest, GenerateGlobalFunctionToGLSL_NullAndFullFunction) {
    EXPECT_EQ(Ccino::CppParser::GenerateGlobalFunctionToGLSL(nullptr), "");

    const auto result = RunProcessStmtOnCode(kShaderTestPreamble + R"(
        Ccino::Vec3 TestEntry(const Ccino::Vec3& dir, int steps) {
            Ccino::Vec3 p(0, 0, 0);
            for (int i = 0; i < steps; ++i) {
                p = p + dir;
            }
            return glm::normalize(p);
        }
    )");

    const std::string expectedFunc =
        "vec3 TestEntry(vec3 dir, int steps) {\n"
        "vec3 p = vec3(0, 0, 0);\n"
        "for (int i = 0;\n(i < steps);(++i)) { \n"
        "(p = (p + dir));\n"
        "}"
        "return normalize(p);\n"
        "}\n";

    EXPECT_EQ(result.FuncGlsl, expectedFunc);
}

