#include <gtest/gtest.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/Tooling.h>
#include <unordered_map>

#include "CodeGenerator/CppParser.h"

namespace {

using namespace clang;
using namespace clang::ast_matchers;

class VarTypeCollector : public MatchFinder::MatchCallback {
public:
    std::unordered_map<std::string, std::string> VarGLSLTypes;
    std::unordered_map<std::string, std::string> VarSSBOReferences;
    std::unordered_map<std::string, std::string> FuncReturnGLSLTypes;

    void run(const MatchFinder::MatchResult& Result) override {
        if (const auto* Var = Result.Nodes.getNodeAs<VarDecl>("var")) {
            VarGLSLTypes[Var->getNameAsString()] =
                Ccino::CppParser::ChangeCppTypeToGLSLType(Var->getType());
            VarSSBOReferences[Var->getNameAsString()] =
                Ccino::CppParser::ChangeAccessorToSSBOReference(const_cast<VarDecl*>(Var));
        }
        if (const auto* Func = Result.Nodes.getNodeAs<FunctionDecl>("func")) {
            FuncReturnGLSLTypes[Func->getNameAsString()] =
                Ccino::CppParser::ChangeCppTypeToGLSLType(Func->getReturnType());
        }
    }
};

VarTypeCollector CollectTypesFromCode(const std::string& code) {
    VarTypeCollector collector;
    MatchFinder finder;

    finder.addMatcher(
        varDecl(hasAncestor(functionDecl(hasName("TestEntry")))).bind("var"),
        &collector
    );
    finder.addMatcher(
        functionDecl(hasName("TestEntry")).bind("func"),
        &collector
    );

    EXPECT_TRUE(tooling::runToolOnCodeWithArgs(
        tooling::newFrontendActionFactory(&finder)->create(),
        code,
        {"-std=c++20"}
    ));
    return collector;
}

} // namespace

TEST(runToolOnCode, CanSyntaxCheckCode) {
    EXPECT_TRUE(clang::tooling::runToolOnCode(std::make_unique<clang::SyntaxOnlyAction>(), "class X {};"));
}

TEST(CppParserTest, ChangeCppTypeToGLSLType_NullQualType) {
    EXPECT_EQ(Ccino::CppParser::ChangeCppTypeToGLSLType(clang::QualType{}), "");
}

TEST(CppParserTest, ChangeCppTypeToGLSLType_BuiltinAndQualifiers) {
    const char* code = R"(
        using uint32_t = unsigned int;

        void TestEntry() {
            bool b = true;
            int i = -1;
            unsigned int u1 = 1u;
            uint32_t u2 = 2u;
            float f = 1.0f;
            double d = 2.0;

            const float& cref_f = f;
            auto auto_u = u2 + 1u;
            auto auto_f = f * 2.0f;
        }
    )";

    auto result = CollectTypesFromCode(code);

    EXPECT_EQ(result.FuncReturnGLSLTypes["TestEntry"], "void");
    EXPECT_EQ(result.VarGLSLTypes["b"], "bool");
    EXPECT_EQ(result.VarGLSLTypes["i"], "int");
    EXPECT_EQ(result.VarGLSLTypes["u1"], "uint");
    EXPECT_EQ(result.VarGLSLTypes["u2"], "uint");
    EXPECT_EQ(result.VarGLSLTypes["f"], "float");
    EXPECT_EQ(result.VarGLSLTypes["d"], "double");
    EXPECT_EQ(result.VarGLSLTypes["cref_f"], "float");
    EXPECT_EQ(result.VarGLSLTypes["auto_u"], "uint");
    EXPECT_EQ(result.VarGLSLTypes["auto_f"], "float");
}

TEST(CppParserTest, ChangeCppTypeToGLSLType_CcinoVectorsAndAutoDeduction) {
    const char* code = R"(
        namespace glm {
            enum qualifier { defaultp };
            template<int L, typename T, qualifier Q = defaultp>
            struct vec {
                vec operator+(const vec& rhs) const { return *this; }
            };
        }

        namespace Ccino {
            using Vec2 = glm::vec<2, float>;
            using Vec3 = glm::vec<3, float>;
            using Vec4 = glm::vec<4, float>;

            using DVec2 = glm::vec<2, double>;
            using DVec3 = glm::vec<3, double>;
            using DVec4 = glm::vec<4, double>;

            using IVec2 = glm::vec<2, int>;
            using IVec3 = glm::vec<3, int>;
            using IVec4 = glm::vec<4, int>;

            using UVec2 = glm::vec<2, unsigned int>;
            using UVec3 = glm::vec<3, unsigned int>;
            using UVec4 = glm::vec<4, unsigned int>;

            using BVec2 = glm::vec<2, bool>;
            using BVec3 = glm::vec<3, bool>;
            using BVec4 = glm::vec<4, bool>;
        }

        void TestEntry() {
            using namespace Ccino;
            Vec2 v2; Vec3 v3; Vec4 v4;
            DVec2 dv2; DVec3 dv3; DVec4 dv4;
            IVec2 iv2; IVec3 iv3; IVec4 iv4;
            UVec2 uv2; UVec3 uv3; UVec4 uv4;
            BVec2 bv2; BVec3 bv3; BVec4 bv4;

            // 测试 auto 推导（丢失 using Vec3 别名，退化为 glm::vec<3, float, defaultp>）
            auto deduced_v3 = v3 + v3;
            const Vec4& ref_v4 = v4;
        }
    )";

    auto result = CollectTypesFromCode(code);

    EXPECT_EQ(result.VarGLSLTypes["v2"], "vec2");
    EXPECT_EQ(result.VarGLSLTypes["v3"], "vec3");
    EXPECT_EQ(result.VarGLSLTypes["v4"], "vec4");

    EXPECT_EQ(result.VarGLSLTypes["dv2"], "dvec2");
    EXPECT_EQ(result.VarGLSLTypes["dv3"], "dvec3");
    EXPECT_EQ(result.VarGLSLTypes["dv4"], "dvec4");

    EXPECT_EQ(result.VarGLSLTypes["iv2"], "ivec2");
    EXPECT_EQ(result.VarGLSLTypes["iv3"], "ivec3");
    EXPECT_EQ(result.VarGLSLTypes["iv4"], "ivec4");

    EXPECT_EQ(result.VarGLSLTypes["uv2"], "uvec2");
    EXPECT_EQ(result.VarGLSLTypes["uv3"], "uvec3");
    EXPECT_EQ(result.VarGLSLTypes["uv4"], "uvec4");

    EXPECT_EQ(result.VarGLSLTypes["bv2"], "bvec2");
    EXPECT_EQ(result.VarGLSLTypes["bv3"], "bvec3");
    EXPECT_EQ(result.VarGLSLTypes["bv4"], "bvec4");

    EXPECT_EQ(result.VarGLSLTypes["deduced_v3"], "vec3");
    EXPECT_EQ(result.VarGLSLTypes["ref_v4"], "vec4");
}

TEST(CppParserTest, ChangeCppTypeToGLSLType_CcinoMatrices) {
    const char* code = R"(
        namespace glm {
            enum qualifier { defaultp };
            template<int C, int R, typename T, qualifier Q = defaultp>
            struct mat {};
        }

        namespace Ccino {
            using Mat2 = glm::mat<2, 2, float>;
            using Mat3 = glm::mat<3, 3, float>;
            using Mat4 = glm::mat<4, 4, float>;
            using Mat2x3 = glm::mat<2, 3, float>;
            using Mat3x4 = glm::mat<3, 4, float>;
            using Mat4x2 = glm::mat<4, 2, float>;

            using DMat2 = glm::mat<2, 2, double>;
            using DMat3 = glm::mat<3, 3, double>;
            using DMat4 = glm::mat<4, 4, double>;
            using DMat2x4 = glm::mat<2, 4, double>;
            using DMat4x3 = glm::mat<4, 3, double>;
        }

        void TestEntry() {
            using namespace Ccino;
            Mat2 m2; Mat3 m3; Mat4 m4;
            Mat2x3 m2x3; Mat3x4 m3x4; Mat4x2 m4x2;
            DMat2 dm2; DMat3 dm3; DMat4 dm4;
            DMat2x4 dm2x4; DMat4x3 dm4x3;
        }
    )";

    auto result = CollectTypesFromCode(code);

    EXPECT_EQ(result.VarGLSLTypes["m2"], "mat2");
    EXPECT_EQ(result.VarGLSLTypes["m3"], "mat3");
    EXPECT_EQ(result.VarGLSLTypes["m4"], "mat4");
    EXPECT_EQ(result.VarGLSLTypes["m2x3"], "mat2x3");
    EXPECT_EQ(result.VarGLSLTypes["m3x4"], "mat3x4");
    EXPECT_EQ(result.VarGLSLTypes["m4x2"], "mat4x2");

    EXPECT_EQ(result.VarGLSLTypes["dm2"], "dmat2");
    EXPECT_EQ(result.VarGLSLTypes["dm3"], "dmat3");
    EXPECT_EQ(result.VarGLSLTypes["dm4"], "dmat4");
    EXPECT_EQ(result.VarGLSLTypes["dm2x4"], "dmat2x4");
    EXPECT_EQ(result.VarGLSLTypes["dm4x3"], "dmat4x3");
}

TEST(CppParserTest, ChangeCppTypeToGLSLType_ArrayAndCustomStruct) {
    const char* code = R"(
        namespace std {
            template<typename T, unsigned long long N>
            struct array {};
        }
        namespace glm {
            template<int L, typename T>
            struct vec {};
        }
        namespace Ccino {
            template<typename T, unsigned long long N>
            using Array = std::array<T, N>;
            using Vec3 = glm::vec<3, float>;
        }

        struct Particle {
            Ccino::Vec3 pos;
            float mass;
        };

        void TestEntry() {
            Ccino::Array<float, 4> weights;
            Ccino::Array<Ccino::Vec3, 16> positions;
            Particle p;
            const auto& p_ref = p;
        }
    )";

    auto result = CollectTypesFromCode(code);

    EXPECT_EQ(result.VarGLSLTypes["weights"], "float[4]");
    EXPECT_EQ(result.VarGLSLTypes["positions"], "vec3[16]");
    EXPECT_EQ(result.VarGLSLTypes["p"], "Particle");
    EXPECT_EQ(result.VarGLSLTypes["p_ref"], "Particle");
}

TEST(CppParserTest, ChangeAccessorToSSBOReference_ReadWriteAccessFlags) {
    const char* code = R"(
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
            struct array {};
        }
        namespace glm {
            template<int L, typename T>
            struct vec {};
        }
        namespace Ccino {
            template<typename T, unsigned long long N>
            using Array = std::array<T, N>;
            using Vec3 = glm::vec<3, float>;

            template<typename BufferResourceType, BufferAccessFlags bufferAccessFlags>
            class Accessor {};
        }

        struct Particle {
            Ccino::Vec3 pos;
            float mass;
            Ccino::Array<int, 4> indices;
        };

        void TestEntry() {
            Ccino::Accessor<Particle, BufferAccessFlagBits::Read> readAc;
            Ccino::Accessor<Particle, BufferAccessFlagBits::Write> writeAc;
            Ccino::Accessor<Particle, BufferAccessFlagBits::ReadWrite> rwAc;

            const auto& readAcRef = readAc;
            Particle normalStruct;
            int normalInt = 0;
        }
    )";

    auto result = CollectTypesFromCode(code);

    const std::string expectedBody =
        "Particle {\n"
        "    vec3 pos;\n"
        "    float mass;\n"
        "    int[4] indices;\n"
        "};\n";

    EXPECT_EQ(
        result.VarSSBOReferences["readAc"],
        "layout(buffer_reference, std430) readonly buffer " + expectedBody
    );
    EXPECT_EQ(
        result.VarSSBOReferences["writeAc"],
        "layout(buffer_reference, std430) writeonly buffer " + expectedBody
    );
    EXPECT_EQ(
        result.VarSSBOReferences["rwAc"],
        "layout(buffer_reference, std430) readwrite buffer " + expectedBody
    );
    EXPECT_EQ(
        result.VarSSBOReferences["readAcRef"],
        "layout(buffer_reference, std430) readonly buffer " + expectedBody
    );

    // 非 Accessor 变量应返回空字符串
    EXPECT_EQ(result.VarSSBOReferences["normalStruct"], "");
    EXPECT_EQ(result.VarSSBOReferences["normalInt"], "");
}

TEST(CppParserTest, ChangeAccessorToSSBOReference_InvalidSecondArgReturnsEmpty) {
    const char* code = R"(
        namespace Ccino {
            template<typename BufferResourceType, typename InvalidSecondArg>
            class Accessor {};
        }

        struct Particle {
            float mass;
        };

        void TestEntry() {
            Ccino::Accessor<Particle, int> invalidAc;
        }
    )";

    auto result = CollectTypesFromCode(code);
    EXPECT_EQ(result.VarSSBOReferences["invalidAc"], "");
}


