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
// 1. ProcessComputeShader 端到端集成测试 (ProcessComputeShaderTest)
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

                auto cs = [inParticles, outParticles, debugOut, dt, gravity, maxSteps, worldMat]() {
                    Ccino::UVec3 globalID = Ccino::ComputeGlobalID;
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

    // 5. 验证完整生成的 GLSL 文本（含 Header、BufferReference 声明、UniformData、PushConstant 与 main 函数体）
    const std::string expectedHeader =
        "#version 460\n"
        "#extension GL_EXT_buffer_reference2 : require\n"
        "#extension GL_EXT_scalar_block_layout : require\n"
        "#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require\n"
        "layout(local_size_x = 64, local_size_y = 4, local_size_z = 1) in;\n";
    const std::string expectedParticleBuf =
        "layout(buffer_reference, scalar) buffer Particle{ \n"
        "vec3 pos;\n"
        "float mass;\n"
        "};\n";
    const std::string expectedDebugBuf =
        "layout(buffer_reference, scalar) buffer DebugInfo{ \n"
        "int stepCount;\n"
        "uint flags;\n"
        "};\n";
    const std::string expectedTail =
        "layout(buffer_reference, scalar, buffer_reference_align = 8) readonly buffer UniformData { \n"
        "Particle inParticles;\n"
        "Particle outParticles;\n"
        "DebugInfo debugOut;\n"
        "float dt;\n"
        "vec3 gravity;\n"
        "int maxSteps;\n"
        "mat4 worldMat;\n"
        "};\n"
        "layout(push_constant) uniform PC {\n"
        "    UniformData uniformData;\n"
        "} pc;\n"
        "void main() { \n"
        "uvec3 globalID = gl_GlobalInvocationID;\n"
        "uint idx = globalID.x;\n"
        "vec3 vel = (pc.uniformData.inParticles[idx].pos + pc.uniformData.gravity);\n"
        "int step = 0;\n"
        "for (int i = 0;(i < pc.uniformData.maxSteps);(++i)) {\n"
        "(vel = (vel + pc.uniformData.gravity));\n"
        "if ((i == 2)) {\n"
        "continue;\n"
        "}\n"
        "else {\n"
        "(step += 1);\n"
        "}\n"
        "}\n"
        "while ((step > 10)) {\n"
        "(step -= 1);\n"
        "}\n"
        "(pc.uniformData.outParticles[idx].pos = vel);\n"
        "(pc.uniformData.debugOut[0].stepCount = step);\n"
        "barrier();\n"
        "}\n";

    EXPECT_EQ(ir.glsl.find(expectedHeader), 0u) << ir.glsl;
    const size_t particlePos = ir.glsl.find(expectedParticleBuf);
    const size_t debugPos    = ir.glsl.find(expectedDebugBuf);
    const size_t tailPos     = ir.glsl.find(expectedTail);
    ASSERT_NE(particlePos, std::string::npos) << ir.glsl;
    ASSERT_NE(debugPos, std::string::npos) << ir.glsl;
    ASSERT_NE(tailPos, std::string::npos) << ir.glsl;
    EXPECT_LT(particlePos, tailPos);
    EXPECT_LT(debugPos, tailPos);
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

                auto cs = [inA, inB, outC]() {
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
        "layout(buffer_reference, scalar) buffer Particle{ \n"
        "vec3 pos;\n"
        "float mass;\n"
        "};\n";
    const size_t firstPos = ir.glsl.find(particleBlock);
    ASSERT_NE(firstPos, std::string::npos);
    EXPECT_EQ(ir.glsl.find(particleBlock, firstPos + 1), std::string::npos);

    // 但 UniformData 和 DescriptorSlotLayout 中必须保留全部 3 个字段
    EXPECT_NE(ir.glsl.find("Particle inA;\nParticle inB;\nParticle outC;\n"), std::string::npos);
    ASSERT_EQ(ir.descriptorSlotLayout.size(), 3u);
    EXPECT_EQ(ir.descriptorSlotLayout[0].name, "inA");
    EXPECT_EQ(ir.descriptorSlotLayout[1].name, "inB");
    EXPECT_EQ(ir.descriptorSlotLayout[2].name, "outC");
    ASSERT_EQ(ir.uniformDataBindings.size(), 3u);
}

TEST(ProcessComputeShaderTest, NestedStructAndBufferReferenceRecursiveEmission) {
    const auto shaders = RunProcessComputeShaderOnCode(
        kShaderTestPreamble + R"(
            struct LeafPayload {
                float weight;
                int id;
            };

            struct InnerHeader {
                LeafPayload leaf;
                Ccino::Vec3 normal;
            };

            struct OuterBufferBlock {
                InnerHeader header;
                LeafPayload extraLeaves[2];
            };

            void TestEntry() {
                Ccino::Accessor<OuterBufferBlock, BufferAccessFlagBits::ReadWrite> blocks;

                auto cs = [blocks]() {
                    uint32_t idx = Ccino::ComputeGlobalID.x;
                    blocks[idx].header.leaf.weight = 1.0f;
                };
            }
        )",
        "NestedBufferRefPass",
        Ccino::ThreadGroupSize{32, 1, 1});

    ASSERT_EQ(shaders.size(), 1u);
    const std::string& glsl = shaders[0].glsl;

    const std::string expectedLeafStruct =
        "struct LeafPayload{\n"
        "float weight;\n"
        "int id;\n"
        "};\n";
    const std::string expectedInnerStruct =
        "struct InnerHeader{\n"
        "LeafPayload leaf;\n"
        "vec3 normal;\n"
        "};\n";
    const std::string expectedOuterBuffer =
        "layout(buffer_reference, scalar) buffer OuterBufferBlock{ \n"
        "InnerHeader header;\n"
        "LeafPayload extraLeaves[2];\n"
        "};\n";

    const size_t leafPos    = glsl.find(expectedLeafStruct);
    const size_t innerPos   = glsl.find(expectedInnerStruct);
    const size_t outerPos   = glsl.find(expectedOuterBuffer);
    const size_t uniformPos = glsl.find("readonly buffer UniformData");

    ASSERT_NE(leafPos, std::string::npos) << glsl;
    ASSERT_NE(innerPos, std::string::npos) << glsl;
    ASSERT_NE(outerPos, std::string::npos) << glsl;
    ASSERT_NE(uniformPos, std::string::npos) << glsl;

    // 拓扑序：LeafPayload -> InnerHeader -> OuterBufferBlock -> UniformData
    EXPECT_LT(leafPos, innerPos);
    EXPECT_LT(innerPos, outerPos);
    EXPECT_LT(outerPos, uniformPos);
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
                uint64_t u64 = 0;
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

                auto cs = [u64, d, f, i, u, b, v2, v3, v4, iv2, iv4, uv2, uv4, bv2, bv4, dv2, dv3, dv4, m2, m3, m4, m2x3, m3x4, dm2, dm4x3]() {
                    int dummy = i;
                };
            }
        )",
        "AllUniformTypesPass",
        Ccino::ThreadGroupSize{1, 1, 1});

    ASSERT_EQ(shaders.size(), 1u);
    const Ccino::ShaderIR& ir = shaders[0];

    const Ccino::DescriptorSlotLayout expectedLayout = {
        {"u64",   Ccino::UniformDataType::UInt64},
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
        uint64_t u64;
        double d;
        float f;
        int i;
        uint32_t u;
        bool b;
    };
    EXPECT_EQ(ir.uniformDataBindings[0].offset, offsetof(ExpectedScalarCapturePrefix, u64));
    EXPECT_EQ(ir.uniformDataBindings[1].offset, offsetof(ExpectedScalarCapturePrefix, d));
    EXPECT_EQ(ir.uniformDataBindings[2].offset, offsetof(ExpectedScalarCapturePrefix, f));
    EXPECT_EQ(ir.uniformDataBindings[3].offset, offsetof(ExpectedScalarCapturePrefix, i));
    EXPECT_EQ(ir.uniformDataBindings[4].offset, offsetof(ExpectedScalarCapturePrefix, u));
    EXPECT_EQ(ir.uniformDataBindings[5].offset, offsetof(ExpectedScalarCapturePrefix, b));
}

TEST(ProcessComputeShaderTest, BuiltInParameterNameReplacementAndWordBoundary) {
    // 1. 验证内建全局变量名被替换为对应的 GLSL 内建变量，且不误伤包含该子串的其他标识符
    const auto withParamShaders = RunProcessComputeShaderOnCode(
        kShaderTestPreamble + R"(
            void TestEntry() {
                auto cs = [](int secondParam) {
                    Ccino::UVec3 tid_copy = Ccino::ComputeGlobalID;
                    Ccino::UVec3 lid_copy = Ccino::ComputeLocalID;
                    uint32_t lidx = Ccino::ComputeLocalIndex;
                    int ComputeGlobalID_local = secondParam + 1;
                    int my_ComputeGlobalID = ComputeGlobalID_local * 2;
                };
            }
        )",
        "WordBoundaryPass",
        Ccino::ThreadGroupSize{16, 16, 1});

    ASSERT_EQ(withParamShaders.size(), 1u);
    const std::string& glsl = withParamShaders[0].glsl;
    EXPECT_NE(glsl.find("uvec3 tid_copy = gl_GlobalInvocationID;\n"), std::string::npos);
    EXPECT_NE(glsl.find("uvec3 lid_copy = gl_LocalInvocationID;\n"), std::string::npos);
    EXPECT_NE(glsl.find("uint lidx = gl_LocalInvocationIndex;\n"), std::string::npos);
    EXPECT_NE(glsl.find("int ComputeGlobalID_local = (secondParam + 1);\n"), std::string::npos);
    EXPECT_NE(glsl.find("int my_ComputeGlobalID = (ComputeGlobalID_local * 2);\n"), std::string::npos);

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

                auto cs = [=, scale = 2.5f]() {
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
    EXPECT_NE(ir.glsl.find("float scale;\n"), std::string::npos);
    EXPECT_NE(ir.glsl.find("vec3 usedDir;\n"), std::string::npos);
    EXPECT_NE(ir.glsl.find("int baseCount;\n"), std::string::npos);
    EXPECT_NE(ir.glsl.find("vec3 d = pc.uniformData.usedDir;\n"), std::string::npos);
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
                    auto cs = [raw]() {};
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

                    rg.AddCS<Ccino::ThreadGroupSize{64, 4, 1}>([=]() {
                        uint32_t idx = Ccino::ComputeGlobalID.x;
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

    const Ccino::DescriptorSlotLayout expectedLayout = {
        {"inOutParticles", Ccino::UniformDataType::Buffer},
        {"gravity",        Ccino::UniformDataType::Vec3},
        {"dt",             Ccino::UniformDataType::Float},
    };
    EXPECT_EQ(ir.descriptorSlotLayout, expectedLayout);
    ASSERT_EQ(ir.uniformDataBindings.size(), 3u);

    EXPECT_NE(ir.glsl.find("layout(local_size_x = 64, local_size_y = 4, local_size_z = 1) in;\n"), std::string::npos);
    EXPECT_NE(ir.glsl.find("layout(buffer_reference, scalar) buffer Particle{ \n"), std::string::npos);
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
                    rg.AddCS<Ccino::ThreadGroupSize{1, 1, 1}>([]() {
                        uint32_t idx = Ccino::ComputeGlobalID.x;
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
                    rg.AddCS<Ccino::ThreadGroupSize{1, 1, 1}>([]() {
                        uint32_t idx = Ccino::ComputeGlobalID.x;
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

                    rg.AddCS<Ccino::ThreadGroupSize{32, 1, 1}>([=]() {
                        if (Ccino::ComputeGlobalID.x < limit) {
                            outCounter[Ccino::ComputeGlobalID.x].count = Ccino::ComputeGlobalID.x;
                        }
                    });
                }).AddPass<"LightingPass">([&](Ccino::RGBuilder& rg, RID& passRID) {
                    auto inCounter = rg.Read<CounterBuf>(bufA, passRID);
                    auto outColor = rg.Write<CounterBuf>(bufB, passRID);
                    uint32_t scale = 2;

                    auto cs = [=]() {
                        outColor[Ccino::ComputeGlobalID.x].count = inCounter[Ccino::ComputeGlobalID.x].count * scale;
                    };
                    rg.AddCS<Ccino::ThreadGroupSize{16, 8, 2}>(cs);
                });
            }
        )");

    const std::string compileCommandsPath = ws.WriteCompileCommands({cppPath});
    const std::vector<Ccino::ShaderIR> shaders = Ccino::CppParser::ParseFilePassShaders(compileCommandsPath);

    ASSERT_EQ(shaders.size(), 2u);

    EXPECT_EQ(shaders[0].passName, "CullPass");
    EXPECT_NE(shaders[0].glsl.find("layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;\n"), std::string::npos);
    EXPECT_NE(shaders[0].glsl.find("(gl_GlobalInvocationID.x < pc.uniformData.limit)"), std::string::npos);

    EXPECT_EQ(shaders[1].passName, "LightingPass");
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
                    rg.AddCS<Ccino::ThreadGroupSize{8, 1, 1}>([=]() {
                        out[Ccino::ComputeGlobalID.x].value = 1.0f;
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
                    rg.AddCS<Ccino::ThreadGroupSize{64, 1, 1}>([=]() {
                        out[Ccino::ComputeGlobalID.x].value = factor;
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
                    rg.AddCS<Ccino::ThreadGroupSize{128, 2, 1}>([=]() {
                        out[Ccino::ComputeGlobalID.x].value = 2.0f;
                    });
                });
            }
        )");

    const std::string compileCommandsPath = ws.WriteCompileCommands({fileA, fileB});
    const std::vector<Ccino::ShaderIR> shaders = Ccino::CppParser::ParseFilePassShaders(compileCommandsPath);

    // 头文件 common_pass_helper.h 中的 HeaderPassShouldBeIgnored 应被 isWrittenInMainFile 过滤掉
    ASSERT_EQ(shaders.size(), 2u);
    EXPECT_EQ(shaders[0].passName, "FileAPass");
    EXPECT_NE(shaders[0].glsl.find("layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;\n"), std::string::npos);
    EXPECT_EQ(shaders[1].passName, "FileBPass");
    EXPECT_NE(shaders[1].glsl.find("layout(local_size_x = 128, local_size_y = 2, local_size_z = 1) in;\n"), std::string::npos);
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




