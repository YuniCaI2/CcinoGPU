#include <gtest/gtest.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/Tooling.h>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "CodeGenerator/CppParser.h"
#include "RHI/DescriptorSlot.h"

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
                T& operator[](unsigned long long i) { return elems[i]; }
                const T& operator[](unsigned long long i) const { return elems[i]; }
            };
        }

        namespace glm {
            enum qualifier { defaultp };

            template<int L, typename T, qualifier Q = defaultp>
            struct vec {
                T x{}, y{}, z{}, w{};
                vec() = default;
                vec(T s) {}
                vec(T x, T y) {}
                vec(T x, T y, T z) {}
                vec(T x, T y, T z, T w) {}

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

            struct ComputeGlobalID {
                uint32_t x;
                uint32_t y;
                uint32_t z;
            };

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
                return glm::dot( args... );
            }

            template <typename... Args>
            constexpr decltype(auto) Cross(Args&&... args) {
                return glm::cross( args... );
            }

            template <typename... Args>
            constexpr decltype(auto) Normalize(Args&&... args) {
                return glm::normalize( args... );
            }

            template <typename... Args>
            constexpr decltype(auto) Clamp(Args&&... args) {
                return glm::clamp( args... );
            }
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

TEST(ProcessStmtTest, ExprStmt_MemberAccessSubscriptAndCcinoWrappers) {
    const auto result = RunProcessStmtOnCode(kShaderTestPreamble + R"(
        struct Particle {
            Ccino::Vec3 pos;
            float mass;
        };

        void TestEntry(Ccino::Accessor<Particle, BufferAccessFlagBits::ReadWrite> buf) {
            int rawArr[4] = {1, 2, 3, 4};
            Ccino::Array<float, 4> stdArr;
            Ccino::Vec3 v(1.0f, 2.0f, 3.0f);

            rawArr[2] = rawArr[0] + 1;
            stdArr[1] = v.x + v[2];
            buf[0].pos = Ccino::Normalize(Ccino::Cross(v, buf[1].pos));
            buf[0].mass = Ccino::Dot(v, buf[0].pos);
            rawArr[3] = Ccino::Clamp(rawArr[2], 0, 10);
        }
    )");

    ASSERT_EQ(result.TopLevelStmtGlsls.size(), 8u);
    EXPECT_EQ(result.TopLevelStmtGlsls[3], "(rawArr[2] = (rawArr[0] + 1));\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[4], "(stdArr[1] = (v.x + v[2]));\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[5], "(buf[0].pos = normalize(cross(v, buf[1].pos)));\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[6], "(buf[0].mass = dot(v, buf[0].pos));\n");
    EXPECT_EQ(result.TopLevelStmtGlsls[7], "(rawArr[3] = clamp(rawArr[2], 0, 10));\n");
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

// ============================================================================
// 11. ProcessComputeShader 端到端集成测试 (ProcessComputeShaderTest)
// ============================================================================

namespace
{
    class ComputeShaderLambdaCollector : public MatchFinder::MatchCallback
    {
    public:
        std::string PassName = "TestComputePass";
        Ccino::ThreadGroupSize GroupSize{64, 1, 1};
        std::vector<Ccino::ShaderIR> Shaders;

        void run(const MatchFinder::MatchResult& Result) override {
            const auto* Lambda = Result.Nodes.getNodeAs<LambdaExpr>("lambda");
            if (!Lambda || !Result.Context) return;
            Shaders.push_back(
                Ccino::CppParser::ProcessComputeShader(Lambda, *Result.Context, PassName, GroupSize));
        }
    };

    std::vector<Ccino::ShaderIR> RunProcessComputeShaderOnCode(
        const std::string& code,
        const std::string& passName = "TestComputePass",
        Ccino::ThreadGroupSize groupSize = {64, 1, 1})
    {
        ComputeShaderLambdaCollector collector;
        collector.PassName = passName;
        collector.GroupSize = groupSize;

        MatchFinder finder;
        finder.addMatcher(
            lambdaExpr(hasAncestor(functionDecl(hasName("TestEntry")))).bind("lambda"),
            &collector);

        EXPECT_TRUE(tooling::runToolOnCodeWithArgs(
            tooling::newFrontendActionFactory(&finder)->create(),
            code,
            {"-std=c++20"}));
        return collector.Shaders;
    }
}  // namespace

TEST(ProcessComputeShaderTest, FullComputeShaderPipelineIntegration) {
    const auto shaders = RunProcessComputeShaderOnCode(
        kShaderTestPreamble + R"(
            struct Particle {
                Ccino::Vec3 pos;
                float mass;
            };

            struct DebugInfo {
                int stepCount;
                uint32_t flags;
            };

            void TestEntry() {
                Ccino::Accessor<Particle, BufferAccessFlagBits::Read> inParticles;
                Ccino::Accessor<Particle, BufferAccessFlagBits::ReadWrite> outParticles;
                Ccino::Accessor<DebugInfo, BufferAccessFlagBits::Write> debugOut;
                float dt = 0.016f;
                Ccino::Vec3 gravity(0, -9, 0);
                int maxSteps = 4;
                Ccino::Mat4 worldMat;

                auto cs = [inParticles, outParticles, debugOut, dt, gravity, maxSteps, worldMat](Ccino::UVec3 threadID) {
                    Ccino::UVec3 globalID = threadID;
                    uint32_t idx = globalID.x;
                    Ccino::Vec3 vel = inParticles[idx].pos + gravity;
                    int step = 0;

                    for (int i = 0; i < maxSteps; ++i) {
                        vel = vel + gravity;
                        if (i == 2) {
                            continue;
                        } else {
                            step += 1;
                        }
                    }

                    while (step > 10) {
                        step -= 1;
                    }

                    outParticles[idx].pos = vel;
                    debugOut[0].stepCount = step;
                    glm::barrier();
                };
            }
        )",
        "ParticleSimulatePass",
        Ccino::ThreadGroupSize{64, 4, 1});

    ASSERT_EQ(shaders.size(), 1u);
    const Ccino::ShaderIR& ir = shaders[0];

    // 1. 验证基础元数据
    EXPECT_EQ(ir.passName, "ParticleSimulatePass");
    EXPECT_EQ(ir.shaderType, Ccino::ShaderType::Compute);
    EXPECT_EQ(ir.threadGroupSize.x, 64u);
    EXPECT_EQ(ir.threadGroupSize.y, 4u);
    EXPECT_EQ(ir.threadGroupSize.z, 1u);

    // 2. 验证 DescriptorSlotLayout
    const Ccino::DescriptorSlotLayout expectedLayout = {
        {"inParticles",  Ccino::UniformDataType::Buffer},
        {"outParticles", Ccino::UniformDataType::Buffer},
        {"debugOut",     Ccino::UniformDataType::Buffer},
        {"dt",           Ccino::UniformDataType::Float},
        {"gravity",      Ccino::UniformDataType::Vec3},
        {"maxSteps",     Ccino::UniformDataType::Int},
        {"worldMat",     Ccino::UniformDataType::Mat4},
    };
    EXPECT_EQ(ir.descriptorSlotLayout, expectedLayout);

    // 3. 验证与 RHI DescriptorSlotBinding::GetOffsetsTable 及 JSON 序列化的衔接
    uint32_t totalSlotSize = 0;
    const auto gpuOffsets = Ccino::RHI::DescriptorSlotBinding::GetOffsetsTable(ir.descriptorSlotLayout, totalSlotSize);
    EXPECT_EQ(gpuOffsets.at("inParticles").offset, 0u);
    EXPECT_EQ(gpuOffsets.at("outParticles").offset, 8u);
    EXPECT_EQ(gpuOffsets.at("debugOut").offset, 16u);
    EXPECT_EQ(gpuOffsets.at("dt").offset, 24u);
    EXPECT_EQ(gpuOffsets.at("gravity").offset, 32u);
    EXPECT_EQ(gpuOffsets.at("maxSteps").offset, 44u);
    EXPECT_EQ(gpuOffsets.at("worldMat").offset, 48u);
    EXPECT_EQ(totalSlotSize, 112u);

    const nlohmann::json layoutJson = ir.descriptorSlotLayout;
    EXPECT_EQ(layoutJson.get<Ccino::DescriptorSlotLayout>(), expectedLayout);

    // 4. 验证 UniformDataBindings (Lambda 闭包字段偏移与类型)
    ASSERT_EQ(ir.uniformDataBindings.size(), 7u);
    EXPECT_EQ(ir.uniformDataBindings[0].name, "inParticles");
    EXPECT_EQ(ir.uniformDataBindings[0].uniformDataType, Ccino::UniformDataType::Buffer);
    EXPECT_EQ(ir.uniformDataBindings[0].offset, 0u);
    for (size_t i = 0; i < ir.uniformDataBindings.size(); ++i) {
        EXPECT_EQ(ir.uniformDataBindings[i].name, expectedLayout[i].name);
        EXPECT_EQ(ir.uniformDataBindings[i].uniformDataType, expectedLayout[i].dataType);
    }

    // 5. 验证完整生成的 GLSL 文本
    const std::string expectedGlsl =
        "#version 460\n"
        "#extension GL_EXT_buffer_reference2 : require\n"
        "layout(local_size_x = 64, local_size_y = 4, local_size_z = 1) in;\n"
        "layout(buffer_reference, std430) buffer Particle {\n"
        "    vec3 pos;\n"
        "    float mass;\n"
        "};\n"
        "layout(buffer_reference, std430) buffer DebugInfo {\n"
        "    int stepCount;\n"
        "    uint flags;\n"
        "};\n"
        "layout(buffer_reference, std140, buffer_reference_align = 16) readonly buffer UniformData { \n"
        "Particle inParticles; \n"
        "Particle outParticles; \n"
        "DebugInfo debugOut; \n"
        "float dt; \n"
        "vec3 gravity; \n"
        "int maxSteps; \n"
        "mat4 worldMat; \n"
        "}; \n"
        "layout(push_constant) uniform PC {\n"
        "    UniformData uniformData;\n"
        "} pc;\n"
        "void main() { \n"
        "uvec3 globalID = uvec3(gl_GlobalInvocationID);\n"
        "uint idx = globalID.x;\n"
        "vec3 vel = (pc.uniformData.inParticles[idx].pos + pc.uniformData.gravity);\n"
        "int step = 0;\n"
        "for (int i = 0;\n(i < pc.uniformData.maxSteps);(++i)) { \n"
        "(vel = (vel + pc.uniformData.gravity));\n"
        "if ((i == 2)) \n"
        "{ \n"
        "continue;\n"
        "}\n"
        "else {\n"
        "(step += 1);\n"
        "}\n"
        "}"
        "while ((step > 10)) {\n"
        "(step -= 1);\n"
        "}"
        "(pc.uniformData.outParticles[idx].pos = vel);\n"
        "(pc.uniformData.debugOut[0].stepCount = step);\n"
        "barrier();\n"
        "}\n";

    EXPECT_EQ(ir.glsl, expectedGlsl);
}

TEST(ProcessComputeShaderTest, DuplicateAccessorTypeDeduplication) {
    const auto shaders = RunProcessComputeShaderOnCode(
        kShaderTestPreamble + R"(
            struct Particle {
                Ccino::Vec3 pos;
                float mass;
            };

            void TestEntry() {
                Ccino::Accessor<Particle, BufferAccessFlagBits::Read> inA;
                Ccino::Accessor<Particle, BufferAccessFlagBits::Read> inB;
                Ccino::Accessor<Particle, BufferAccessFlagBits::Write> outC;

                auto cs = [inA, inB, outC](Ccino::UVec3 id) {
                    int x = 1;
                };
            }
        )",
        "DeduplicatePass",
        Ccino::ThreadGroupSize{32, 1, 1});

    ASSERT_EQ(shaders.size(), 1u);
    const Ccino::ShaderIR& ir = shaders[0];

    // 多个相同底层结构体的 Accessor（无论读写权限是否相同）只应生成一次 buffer Particle 声明
    const std::string particleBlock =
        "layout(buffer_reference, std430) buffer Particle {\n"
        "    vec3 pos;\n"
        "    float mass;\n"
        "};\n";
    const size_t firstPos = ir.glsl.find(particleBlock);
    ASSERT_NE(firstPos, std::string::npos);
    EXPECT_EQ(ir.glsl.find(particleBlock, firstPos + 1), std::string::npos);

    // 但 UniformData 和 DescriptorSlotLayout 中必须保留全部 3 个字段
    EXPECT_NE(ir.glsl.find("Particle inA; \nParticle inB; \nParticle outC; \n"), std::string::npos);
    ASSERT_EQ(ir.descriptorSlotLayout.size(), 3u);
    EXPECT_EQ(ir.descriptorSlotLayout[0].name, "inA");
    EXPECT_EQ(ir.descriptorSlotLayout[1].name, "inB");
    EXPECT_EQ(ir.descriptorSlotLayout[2].name, "outC");
    ASSERT_EQ(ir.uniformDataBindings.size(), 3u);
}

TEST(ProcessComputeShaderTest, AllSupportedUniformCaptureTypesAndCpuOffsets) {
    const std::string extraTypesPreamble = R"(
        using uint64_t = unsigned long long;
        namespace Ccino {
            using IVec2 = glm::vec<2, int>;
            using IVec4 = glm::vec<4, int>;
            using UVec2 = glm::vec<2, unsigned int>;
            using UVec4 = glm::vec<4, unsigned int>;
            using BVec2 = glm::vec<2, bool>;
            using BVec4 = glm::vec<4, bool>;
            using DVec2 = glm::vec<2, double>;
            using DVec3 = glm::vec<3, double>;
            using DVec4 = glm::vec<4, double>;
            using Mat2 = glm::mat<2, 2, float>;
            using Mat3 = glm::mat<3, 3, float>;
            using Mat2x3 = glm::mat<2, 3, float>;
            using Mat3x4 = glm::mat<3, 4, float>;
            using DMat2 = glm::mat<2, 2, double>;
            using DMat4x3 = glm::mat<4, 3, double>;
        }
    )";

    const auto shaders = RunProcessComputeShaderOnCode(
        kShaderTestPreamble + extraTypesPreamble + R"(
            void TestEntry() {
                uint64_t bda = 0;
                double d = 1.0;
                float f = 2.0f;
                int i = -3;
                uint32_t u = 4u;
                bool b = true;
                Ccino::Vec2 v2;
                Ccino::Vec3 v3;
                Ccino::Vec4 v4;
                Ccino::IVec2 iv2;
                Ccino::IVec4 iv4;
                Ccino::UVec2 uv2;
                Ccino::UVec4 uv4;
                Ccino::BVec2 bv2;
                Ccino::BVec4 bv4;
                Ccino::DVec2 dv2;
                Ccino::DVec3 dv3;
                Ccino::DVec4 dv4;
                Ccino::Mat2 m2;
                Ccino::Mat3 m3;
                Ccino::Mat4 m4;
                Ccino::Mat2x3 m2x3;
                Ccino::Mat3x4 m3x4;
                Ccino::DMat2 dm2;
                Ccino::DMat4x3 dm4x3;

                auto cs = [bda, d, f, i, u, b, v2, v3, v4, iv2, iv4, uv2, uv4, bv2, bv4, dv2, dv3, dv4, m2, m3, m4, m2x3, m3x4, dm2, dm4x3](Ccino::UVec3 tid) {
                    int dummy = i;
                };
            }
        )",
        "AllUniformTypesPass",
        Ccino::ThreadGroupSize{1, 1, 1});

    ASSERT_EQ(shaders.size(), 1u);
    const Ccino::ShaderIR& ir = shaders[0];

    const Ccino::DescriptorSlotLayout expectedLayout = {
        {"bda",   Ccino::UniformDataType::Buffer},
        {"d",     Ccino::UniformDataType::Double},
        {"f",     Ccino::UniformDataType::Float},
        {"i",     Ccino::UniformDataType::Int},
        {"u",     Ccino::UniformDataType::UInt},
        {"b",     Ccino::UniformDataType::Bool},
        {"v2",    Ccino::UniformDataType::Vec2},
        {"v3",    Ccino::UniformDataType::Vec3},
        {"v4",    Ccino::UniformDataType::Vec4},
        {"iv2",   Ccino::UniformDataType::IVec2},
        {"iv4",   Ccino::UniformDataType::IVec4},
        {"uv2",   Ccino::UniformDataType::UVec2},
        {"uv4",   Ccino::UniformDataType::UVec4},
        {"bv2",   Ccino::UniformDataType::BVec2},
        {"bv4",   Ccino::UniformDataType::BVec4},
        {"dv2",   Ccino::UniformDataType::DVec2},
        {"dv3",   Ccino::UniformDataType::DVec3},
        {"dv4",   Ccino::UniformDataType::DVec4},
        {"m2",    Ccino::UniformDataType::Mat2},
        {"m3",    Ccino::UniformDataType::Mat3},
        {"m4",    Ccino::UniformDataType::Mat4},
        {"m2x3",  Ccino::UniformDataType::Mat2x3},
        {"m3x4",  Ccino::UniformDataType::Mat3x4},
        {"dm2",   Ccino::UniformDataType::DMat2},
        {"dm4x3", Ccino::UniformDataType::DMat4x3},
    };

    EXPECT_EQ(ir.descriptorSlotLayout, expectedLayout);
    ASSERT_EQ(ir.uniformDataBindings.size(), expectedLayout.size());

    // 验证前 6 个基础标量字段在 C++ Lambda 结构体中的精确字节偏移
    struct ExpectedScalarCapturePrefix {
        uint64_t bda;
        double d;
        float f;
        int i;
        uint32_t u;
        bool b;
    };
    EXPECT_EQ(ir.uniformDataBindings[0].offset, offsetof(ExpectedScalarCapturePrefix, bda));
    EXPECT_EQ(ir.uniformDataBindings[1].offset, offsetof(ExpectedScalarCapturePrefix, d));
    EXPECT_EQ(ir.uniformDataBindings[2].offset, offsetof(ExpectedScalarCapturePrefix, f));
    EXPECT_EQ(ir.uniformDataBindings[3].offset, offsetof(ExpectedScalarCapturePrefix, i));
    EXPECT_EQ(ir.uniformDataBindings[4].offset, offsetof(ExpectedScalarCapturePrefix, u));
    EXPECT_EQ(ir.uniformDataBindings[5].offset, offsetof(ExpectedScalarCapturePrefix, b));
}

TEST(ProcessComputeShaderTest, BuiltInParameterNameReplacementAndWordBoundary) {
    // 1. 验证第一个参数名被替换为 gl_GlobalInvocationID，且不误伤包含该子串的其他标识符
    const auto withParamShaders = RunProcessComputeShaderOnCode(
        kShaderTestPreamble + R"(
            void TestEntry() {
                auto cs = [](Ccino::UVec3 tid, int secondParam) {
                    Ccino::UVec3 tid_copy = tid;
                    int tid_local = secondParam + 1;
                    int my_tid = tid_local * 2;
                };
            }
        )",
        "WordBoundaryPass",
        Ccino::ThreadGroupSize{16, 16, 1});

    ASSERT_EQ(withParamShaders.size(), 1u);
    const std::string& glsl = withParamShaders[0].glsl;
    EXPECT_NE(glsl.find("uvec3 tid_copy = uvec3(gl_GlobalInvocationID);\n"), std::string::npos);
    EXPECT_NE(glsl.find("int tid_local = (secondParam + 1);\n"), std::string::npos);
    EXPECT_NE(glsl.find("int my_tid = (tid_local * 2);\n"), std::string::npos);

    // 2. 验证无参数 Lambda 与匿名参数 Lambda 均能正常生成且不崩溃
    const auto noParamShaders = RunProcessComputeShaderOnCode(
        kShaderTestPreamble + R"(
            void TestEntry() {
                auto cs1 = []() {
                    int a = 42;
                };
                auto cs2 = [](Ccino::UVec3) {
                    int b = 99;
                };
            }
        )",
        "NoParamPass",
        Ccino::ThreadGroupSize{0, 0, 8});

    ASSERT_EQ(noParamShaders.size(), 2u);
    EXPECT_NE(noParamShaders[0].glsl.find("layout(local_size_z = 8) in;\n"), std::string::npos);
    EXPECT_NE(noParamShaders[0].glsl.find("int a = 42;\n"), std::string::npos);
    EXPECT_NE(noParamShaders[1].glsl.find("int b = 99;\n"), std::string::npos);
}

TEST(ProcessComputeShaderTest, ImplicitCaptureAndInitCapture) {
    // 测试 [=] 隐式捕获（仅捕获体内 odr-used 的变量）以及 C++14 广义初始化捕获 [scale = 2.0f]
    const auto shaders = RunProcessComputeShaderOnCode(
        kShaderTestPreamble + R"(
            void TestEntry() {
                Ccino::Vec3 usedDir(1, 0, 0);
                Ccino::Vec3 unusedDir(0, 1, 0);
                int baseCount = 8;

                auto cs = [=, scale = 2.5f](Ccino::UVec3 threadID) {
                    Ccino::Vec3 d = usedDir;
                    int total = baseCount + 1;
                    float s = scale;
                };
            }
        )",
        "ImplicitCapturePass",
        Ccino::ThreadGroupSize{8, 8, 1});

    ASSERT_EQ(shaders.size(), 1u);
    const Ccino::ShaderIR& ir = shaders[0];

    // unusedDir 未在 Lambda 体内使用，不应出现在捕获布局中；scale / usedDir / baseCount 均应正确提取名称
    ASSERT_EQ(ir.descriptorSlotLayout.size(), 3u);
    EXPECT_EQ(ir.glsl.find("unusedDir"), std::string::npos);
    EXPECT_NE(ir.glsl.find("float scale; \n"), std::string::npos);
    EXPECT_NE(ir.glsl.find("vec3 usedDir; \n"), std::string::npos);
    EXPECT_NE(ir.glsl.find("int baseCount; \n"), std::string::npos);
    EXPECT_NE(ir.glsl.find("vec3 d = vec3(pc.uniformData.usedDir);\n"), std::string::npos);
    EXPECT_NE(ir.glsl.find("int total = (pc.uniformData.baseCount + 1);\n"), std::string::npos);
    EXPECT_NE(ir.glsl.find("float s = pc.uniformData.scale;\n"), std::string::npos);
}

TEST(ProcessComputeShaderTest, UnsupportedCaptureTypeThrowsInvalidArgument) {
    // 直接捕获非 Accessor 的自定义结构体应抛出 std::invalid_argument
    EXPECT_THROW(
        RunProcessComputeShaderOnCode(
            kShaderTestPreamble + R"(
                struct RawStruct {
                    float a;
                };

                void TestEntry() {
                    RawStruct raw{1.0f};
                    auto cs = [raw](Ccino::UVec3 id) {};
                }
            )"),
        std::invalid_argument);
}

// ============================================================================
// CppParser::ParseFilePassShaders 端到端单元测试 (基于真实 compile_commands.json)
// ============================================================================
namespace
{
    const std::string kRGPassTestPreamble = kShaderTestPreamble + R"(
        struct RID {
            uint32_t index = 0;
        };

        namespace Ccino {
            struct ThreadGroupSize {
                uint32_t x = 1;
                uint32_t y = 1;
                uint32_t z = 1;
            };

            template <unsigned long long N>
            struct FixedString {
                char data[N]{};
                constexpr FixedString(const char (&str)[N]) {
                    for (unsigned long long i = 0; i < N; ++i) {
                        data[i] = str[i];
                    }
                }
            };

            class RGBuilder {
            public:
                template <FixedString passName, typename Func>
                RGBuilder& AddPass(Func&& func) {
                    RID passRID{};
                    func(*this, passRID);
                    return *this;
                }

                template <ThreadGroupSize threadGroupSize, typename CS>
                void AddCS(CS&& cs) {
                    (void)cs;
                }

                template <typename BufferResourceType>
                Accessor<BufferResourceType, BufferAccessFlagBits::Read> Read(RID&, RID&) {
                    return {};
                }

                template <typename BufferResourceType>
                Accessor<BufferResourceType, BufferAccessFlagBits::Write> Write(RID&, RID&) {
                    return {};
                }

                template <typename BufferResourceType>
                Accessor<BufferResourceType, BufferAccessFlagBits::ReadWrite> ReadWrite(RID&, RID&) {
                    return {};
                }
            };
        }
    )";

    class TempCompileCommandsWorkspace
    {
    public:
        explicit TempCompileCommandsWorkspace(const std::string& suiteSubDir) {
            dirPath = std::filesystem::temp_directory_path() / ("CcinoGPU_ParsePassTest_" + suiteSubDir);
            std::error_code ec;
            std::filesystem::remove_all(dirPath, ec);
            std::filesystem::create_directories(dirPath, ec);
        }

        ~TempCompileCommandsWorkspace() {
            std::error_code ec;
            std::filesystem::remove_all(dirPath, ec);
        }

        std::filesystem::path WriteFile(const std::string& relativeName, const std::string& content) const {
            std::filesystem::path filePath = dirPath / relativeName;
            std::filesystem::create_directories(filePath.parent_path());
            std::ofstream ofs(filePath, std::ios::binary);
            ofs << content;
            ofs.close();
            return filePath;
        }

        std::string WriteCompileCommands(const std::vector<std::filesystem::path>& sourceFiles,
                                         const std::vector<std::string>& extraArguments = {}) const {
            nlohmann::json db = nlohmann::json::array();
            const std::string dirStr = dirPath.generic_string();
            for (const auto& src : sourceFiles) {
                const std::string fileStr = src.generic_string();
                std::vector<std::string> arguments = {"clang++", "-std=c++20", "-fsyntax-only", "-I" + dirStr};
                arguments.insert(arguments.end(), extraArguments.begin(), extraArguments.end());
                arguments.push_back(fileStr);
                db.push_back({
                    {"directory", dirStr},
                    {"arguments", arguments},
                    {"file", fileStr}
                });
            }
            const std::filesystem::path jsonPath = dirPath / "compile_commands.json";
            std::ofstream ofs(jsonPath, std::ios::binary);
            ofs << db.dump(2);
            ofs.close();
            return jsonPath.generic_string();
        }

        std::filesystem::path dirPath;
    };
}  // namespace

TEST(ParseFilePassShadersTest, SinglePassInlineComputeShader) {
    TempCompileCommandsWorkspace ws("SinglePass");

    const auto cppPath = ws.WriteFile(
        "single_pass.cpp",
        kRGPassTestPreamble + R"(
            struct Particle {
                Ccino::Vec3 pos;
                float mass;
            };

            void BuildParticleGraph() {
                Ccino::RGBuilder builder;
                RID particleBuf{};

                builder.AddPass<"ParticleSimulatePass">([&](Ccino::RGBuilder& rg, RID& passRID) {
                    auto inOutParticles = rg.ReadWrite<Particle>(particleBuf, passRID);
                    float dt = 0.016f;
                    Ccino::Vec3 gravity(0, -9, 0);

                    rg.AddCS<Ccino::ThreadGroupSize{64, 4, 1}>([=](Ccino::ComputeGlobalID id) {
                        uint32_t idx = id.x;
                        inOutParticles[idx].pos = inOutParticles[idx].pos + gravity * dt;
                    });
                });
            }
        )");

    const std::string compileCommandsPath = ws.WriteCompileCommands({cppPath});
    const std::vector<Ccino::ShaderIR> shaders = Ccino::CppParser::ParseFilePassShaders(compileCommandsPath);

    ASSERT_EQ(shaders.size(), 1u);
    const Ccino::ShaderIR& ir = shaders[0];

    EXPECT_EQ(ir.passName, "ParticleSimulatePass");
    EXPECT_EQ(ir.shaderType, Ccino::ShaderType::Compute);
    EXPECT_EQ(ir.threadGroupSize, (Ccino::ThreadGroupSize{64, 4, 1}));

    const Ccino::DescriptorSlotLayout expectedLayout = {
        {"inOutParticles", Ccino::UniformDataType::Buffer},
        {"gravity",        Ccino::UniformDataType::Vec3},
        {"dt",             Ccino::UniformDataType::Float},
    };
    EXPECT_EQ(ir.descriptorSlotLayout, expectedLayout);
    ASSERT_EQ(ir.uniformDataBindings.size(), 3u);

    EXPECT_NE(ir.glsl.find("layout(local_size_x = 64, local_size_y = 4, local_size_z = 1) in;\n"), std::string::npos);
    EXPECT_NE(ir.glsl.find("layout(buffer_reference, std430) buffer Particle {\n"), std::string::npos);
    EXPECT_NE(ir.glsl.find("uint idx = gl_GlobalInvocationID.x;\n"), std::string::npos);
    EXPECT_NE(ir.glsl.find("pc.uniformData.inOutParticles[idx].pos = (pc.uniformData.inOutParticles[idx].pos + (pc.uniformData.gravity * pc.uniformData.dt))"), std::string::npos);
}

TEST(ParseFilePassShadersTest, BuiltinHeadersAndEndOfOptionsSeparator) {
    TempCompileCommandsWorkspace ws("BuiltinHeadersAndSeparator");
    const auto cppPath = ws.WriteFile(
        "builtin_headers_pass.cpp",
        std::string(R"(
            #include <stddef.h>
            #ifndef __STDDEF_H
            #error Expected Clang's builtin stddef.h
            #endif
            static_assert(sizeof(size_t) == sizeof(void*));
        )") + kRGPassTestPreamble + R"(
            void BuildBuiltinHeadersPass() {
                Ccino::RGBuilder builder;
                builder.AddPass<"BuiltinHeadersPass">([&](Ccino::RGBuilder& rg, RID& passRID) {
                    rg.AddCS<Ccino::ThreadGroupSize{1, 1, 1}>([](Ccino::ComputeGlobalID id) {
                        uint32_t idx = id.x;
                    });
                });
            }
        )");

    // The default resource directory must be inserted before the end-of-options separator.
    const std::string compileCommandsPath = ws.WriteCompileCommands({cppPath}, {"--"});
    const auto shaders = Ccino::CppParser::ParseFilePassShaders(compileCommandsPath);
    ASSERT_EQ(shaders.size(), 1u);
    EXPECT_EQ(shaders[0].passName, "BuiltinHeadersPass");
    EXPECT_NE(shaders[0].glsl.find("uint idx = gl_GlobalInvocationID.x;"), std::string::npos);
}

TEST(ParseFilePassShadersTest, ExplicitResourceDirectoryIsPreserved) {
    TempCompileCommandsWorkspace ws("ExplicitResourceDir");
    ws.WriteFile("resource/include/ccino_resource_marker.h", "#define CCINO_RESOURCE_MARKER 23\n");
    const auto cppPath = ws.WriteFile(
        "explicit_resource_pass.cpp",
        std::string(R"(
            #include <ccino_resource_marker.h>
            static_assert(CCINO_RESOURCE_MARKER == 23);
        )") + kRGPassTestPreamble + R"(
            void BuildExplicitResourcePass() {
                Ccino::RGBuilder builder;
                builder.AddPass<"ExplicitResourcePass">([&](Ccino::RGBuilder& rg, RID& passRID) {
                    rg.AddCS<Ccino::ThreadGroupSize{1, 1, 1}>([](Ccino::ComputeGlobalID id) {
                        uint32_t idx = id.x;
                    });
                });
            }
        )");

    const std::string resourceDir = (ws.dirPath / "resource").generic_string();
    for (const auto& extraArguments : std::vector<std::vector<std::string>>{
             {"-resource-dir=" + resourceDir}, {"-resource-dir", resourceDir}}) {
        const std::string compileCommandsPath = ws.WriteCompileCommands({cppPath}, extraArguments);
        const auto shaders = Ccino::CppParser::ParseFilePassShaders(compileCommandsPath);
        ASSERT_EQ(shaders.size(), 1u);
        EXPECT_EQ(shaders[0].passName, "ExplicitResourcePass");
    }
}

TEST(ParseFilePassShadersTest, VariableLambdaAndChainedPasses) {
    TempCompileCommandsWorkspace ws("ChainedAndVarLambda");

    const auto cppPath = ws.WriteFile(
        "chained_passes.cpp",
        kRGPassTestPreamble + R"(
            struct CounterBuf {
                uint32_t count;
            };

            void BuildChainedGraph() {
                Ccino::RGBuilder builder;
                RID bufA{}, bufB{};

                builder.AddPass<"CullPass">([&](Ccino::RGBuilder& rg, RID& passRID) {
                    auto outCounter = rg.Write<CounterBuf>(bufA, passRID);
                    uint32_t limit = 128;

                    rg.AddCS<Ccino::ThreadGroupSize{32, 1, 1}>([=](Ccino::ComputeGlobalID gid) {
                        if (gid.x < limit) {
                            outCounter[gid.x].count = gid.x;
                        }
                    });
                }).AddPass<"LightingPass">([&](Ccino::RGBuilder& rg, RID& passRID) {
                    auto inCounter = rg.Read<CounterBuf>(bufA, passRID);
                    auto outColor = rg.Write<CounterBuf>(bufB, passRID);
                    uint32_t scale = 2;

                    auto cs = [=](Ccino::ComputeGlobalID tid) {
                        outColor[tid.x].count = inCounter[tid.x].count * scale;
                    };
                    rg.AddCS<Ccino::ThreadGroupSize{16, 8, 2}>(cs);
                });
            }
        )");

    const std::string compileCommandsPath = ws.WriteCompileCommands({cppPath});
    const std::vector<Ccino::ShaderIR> shaders = Ccino::CppParser::ParseFilePassShaders(compileCommandsPath);

    ASSERT_EQ(shaders.size(), 2u);

    EXPECT_EQ(shaders[0].passName, "CullPass");
    EXPECT_EQ(shaders[0].threadGroupSize, (Ccino::ThreadGroupSize{32, 1, 1}));
    EXPECT_NE(shaders[0].glsl.find("layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;\n"), std::string::npos);
    EXPECT_NE(shaders[0].glsl.find("(gl_GlobalInvocationID.x < pc.uniformData.limit)"), std::string::npos);

    EXPECT_EQ(shaders[1].passName, "LightingPass");
    EXPECT_EQ(shaders[1].threadGroupSize, (Ccino::ThreadGroupSize{16, 8, 2}));
    EXPECT_NE(shaders[1].glsl.find("layout(local_size_x = 16, local_size_y = 8, local_size_z = 2) in;\n"), std::string::npos);
    EXPECT_NE(shaders[1].glsl.find("(pc.uniformData.outColor[gl_GlobalInvocationID.x].count = (pc.uniformData.inCounter[gl_GlobalInvocationID.x].count * pc.uniformData.scale));\n"), std::string::npos);
}

TEST(ParseFilePassShadersTest, MultipleFilesAndIgnoreHeaderLambda) {
    TempCompileCommandsWorkspace ws("MultiFilesAndHeader");

    ws.WriteFile(
        "common_pass_helper.h",
        kRGPassTestPreamble + R"(
            struct DataNode {
                float value;
            };

            inline void RegisterHeaderOnlyPass(Ccino::RGBuilder& builder, RID& res) {
                builder.AddPass<"HeaderPassShouldBeIgnored">([&](Ccino::RGBuilder& rg, RID& passRID) {
                    auto out = rg.Write<DataNode>(res, passRID);
                    rg.AddCS<Ccino::ThreadGroupSize{8, 1, 1}>([=](Ccino::ComputeGlobalID id) {
                        out[id.x].value = 1.0f;
                    });
                });
            }
        )");

    const auto fileA = ws.WriteFile(
        "file_a.cpp",
        R"(#include "common_pass_helper.h"
            void EntryA() {
                Ccino::RGBuilder builder;
                RID res{};
                RegisterHeaderOnlyPass(builder, res);
                builder.AddPass<"FileAPass">([&](Ccino::RGBuilder& rg, RID& passRID) {
                    auto out = rg.Write<DataNode>(res, passRID);
                    float factor = 3.5f;
                    rg.AddCS<Ccino::ThreadGroupSize{64, 1, 1}>([=](Ccino::ComputeGlobalID id) {
                        out[id.x].value = factor;
                    });
                });
            }
        )");

    const auto fileB = ws.WriteFile(
        "file_b.cpp",
        R"(#include "common_pass_helper.h"
            void EntryB() {
                Ccino::RGBuilder builder;
                RID res{};
                builder.AddPass<"FileBPass">([&](Ccino::RGBuilder& rg, RID& passRID) {
                    auto out = rg.Write<DataNode>(res, passRID);
                    rg.AddCS<Ccino::ThreadGroupSize{128, 2, 1}>([=](Ccino::ComputeGlobalID id) {
                        out[id.x].value = 2.0f;
                    });
                });
            }
        )");

    const std::string compileCommandsPath = ws.WriteCompileCommands({fileA, fileB});
    const std::vector<Ccino::ShaderIR> shaders = Ccino::CppParser::ParseFilePassShaders(compileCommandsPath);

    // 头文件 common_pass_helper.h 中的 HeaderPassShouldBeIgnored 应被 isWrittenInMainFile 过滤掉
    ASSERT_EQ(shaders.size(), 2u);
    EXPECT_EQ(shaders[0].passName, "FileAPass");
    EXPECT_EQ(shaders[0].threadGroupSize, (Ccino::ThreadGroupSize{64, 1, 1}));
    EXPECT_EQ(shaders[1].passName, "FileBPass");
    EXPECT_EQ(shaders[1].threadGroupSize, (Ccino::ThreadGroupSize{128, 2, 1}));
}

TEST(ParseFilePassShadersTest, InvalidOrEmptyCompilationDatabaseReturnsEmpty) {
    // 1. 不存在的 compile_commands.json 路径
    const auto missingResult = Ccino::CppParser::ParseFilePassShaders("non_existent_dir/compile_commands.json");
    EXPECT_TRUE(missingResult.empty());

    // 2. 空的 compile_commands.json
    TempCompileCommandsWorkspace ws("EmptyDB");
    const std::string emptyJsonPath = ws.WriteCompileCommands({});
    const auto emptyResult = Ccino::CppParser::ParseFilePassShaders(emptyJsonPath);
    EXPECT_TRUE(emptyResult.empty());
}




