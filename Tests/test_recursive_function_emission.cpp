#include <gtest/gtest.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "CodeGenerator/CppParser.h"
#include "GLSLCompiler/GLSLCompiler.h"
#include "ShaderStruct/ShaderIR.h"

namespace {
    // 场景代码写到临时文件里解析, 不放进工程源码: GeneratorMain 不会把它们当成真实 Pass, 失败的场景也不会阻断构建
    const char* kScenarioSource = R"(
#include "Core/CcinoType.h"
#include "RG/RenderGraph.h"

namespace {
    struct OutElem {
        float value;
        float pad0;
        float pad1;
        float pad2;
    };

    // 1. 多层调用: lambda 只调 TopA, MidB / LeafC 只能靠递归收集
    float LeafC(float x) { return x * 2.0f; }
    float MidB(float x) { return LeafC(x) + 1.0f; }
    float TopA(float x) { return MidB(x) * 3.0f; }

    // 2. 菱形调用: DiamondLeaf 被两条路径调用, 只能生成一次
    float DiamondLeaf(float x) { return x + 0.5f; }
    float DiamondLeft(float x) { return DiamondLeaf(x) * 2.0f; }
    float DiamondRight(float x) { return DiamondLeaf(x) * 4.0f; }
    float DiamondTop(float x) { return DiamondLeft(x) + DiamondRight(x); }

    // 3. 成员函数里调用另一个类的成员函数, 之后再隐式访问自己的成员
    struct Spring {
        float k;
        float Stiff() const { return k * 2.0f; }
    };
    struct Body {
        float mass;
        Spring spring;
        float Energy() const {
            float s = spring.Stiff();
            return s * mass;
        }
    };

    // 4. 只在被嵌套调用的辅助函数里出现的局部结构体
    struct LocalPair {
        float a;
        float b;
    };
    float InnerWithStruct(float x) {
        LocalPair p;
        p.a = x;
        p.b = x * 2.0f;
        return p.a + p.b;
    }
    float OuterWithStruct(float x) { return InnerWithStruct(x) + 1.0f; }

    // 5. 多层嵌套结构体 (C 数组 / std::array / 模板特化), 只出现在被嵌套调用的辅助函数里
    struct TreeLeaf {
        float w;
        int id;
    };
    struct TreeMid {
        TreeLeaf leaf;
        Ccino::Vec3 n;
        TreeLeaf extra[2];
    };
    template <typename T>
    struct Box {
        T item;
        float scale;
    };
    struct TreeTop {
        TreeMid mid;
        Box<TreeLeaf> boxed;
        std::array<TreeLeaf, 2> arr;
    };
    float SumTree(TreeTop t) { return t.mid.leaf.w + t.boxed.item.w * t.boxed.scale + t.arr[1].w; }
    float BuildTree(float x) {
        TreeTop t;
        t.mid.leaf.w = x;
        t.boxed.item.w = x;
        t.boxed.scale = 2.0f;
        t.arr[1].w = x;
        return SumTree(t);
    }

    // 6. Buffer 元素里嵌套结构体
    struct NestedElem {
        TreeMid mid;
        float value;
    };
}

void BuildRecursiveScenarioPasses() {
    Ccino::RGBuilder builder;
    RID buf{};
    builder.AddPass<"RecChainPass">([&](Ccino::RGBuilder& rg, RID& passRID) {
        auto result = rg.Write<OutElem>(buf, passRID);
        rg.AddCS<Ccino::ThreadGroupSize{1, 1, 1}>([=]() { result[0].value = TopA(1.0f); }, Ccino::WorkGroupSize{1, 1, 1});
    }).AddPass<"RecDiamondPass">([&](Ccino::RGBuilder& rg, RID& passRID) {
        auto result = rg.Write<OutElem>(buf, passRID);
        rg.AddCS<Ccino::ThreadGroupSize{1, 1, 1}>([=]() { result[0].value = DiamondTop(1.0f); }, Ccino::WorkGroupSize{1, 1, 1});
    }).AddPass<"RecMemberPass">([&](Ccino::RGBuilder& rg, RID& passRID) {
        auto result = rg.Write<OutElem>(buf, passRID);
        rg.AddCS<Ccino::ThreadGroupSize{1, 1, 1}>([=]() {
            Body body;
            body.mass = 2.0f;
            body.spring.k = 3.0f;
            result[0].value = body.Energy();
        }, Ccino::WorkGroupSize{1, 1, 1});
    }).AddPass<"RecNestedStructPass">([&](Ccino::RGBuilder& rg, RID& passRID) {
        auto result = rg.Write<OutElem>(buf, passRID);
        rg.AddCS<Ccino::ThreadGroupSize{1, 1, 1}>([=]() { result[0].value = OuterWithStruct(1.0f); }, Ccino::WorkGroupSize{1, 1, 1});
    }).AddPass<"RecNestedStructTreePass">([&](Ccino::RGBuilder& rg, RID& passRID) {
        auto result = rg.Write<OutElem>(buf, passRID);
        rg.AddCS<Ccino::ThreadGroupSize{1, 1, 1}>([=]() { result[0].value = BuildTree(1.0f); }, Ccino::WorkGroupSize{1, 1, 1});
    }).AddPass<"RecNestedBufferPass">([&](Ccino::RGBuilder& rg, RID& passRID) {
        auto elems = rg.ReadWrite<NestedElem>(buf, passRID);
        rg.AddCS<Ccino::ThreadGroupSize{1, 1, 1}>([=]() {
            elems[1].value = elems[0].mid.extra[1].w + elems[0].mid.n.y;
        }, Ccino::WorkGroupSize{1, 1, 1});
    });
}
)";

    // 5. 自递归: GLSL 本身不允许递归, 生成器应当报错退出, 而不是无限递归
    const char* kSelfRecursionSource = R"(
#include "Core/CcinoType.h"
#include "RG/RenderGraph.h"

namespace {
    struct OutElem {
        float value;
        float pad0;
        float pad1;
        float pad2;
    };
    int Factorial(int n) { return n <= 1 ? 1 : n * Factorial(n - 1); }
}

void BuildSelfRecursionPass() {
    Ccino::RGBuilder builder;
    RID buf{};
    builder.AddPass<"RecSelfPass">([&](Ccino::RGBuilder& rg, RID& passRID) {
        auto result = rg.Write<OutElem>(buf, passRID);
        rg.AddCS<Ccino::ThreadGroupSize{1, 1, 1}>([=]() { result[0].value = float(Factorial(4)); }, Ccino::WorkGroupSize{1, 1, 1});
    });
}
)";

    void ReplaceAll(std::string& text, const std::string& from, const std::string& to) {
        for (size_t pos = text.find(from); pos != std::string::npos; pos = text.find(from, pos + to.size())) {
            text.replace(pos, from.size(), to);
        }
    }

    // 借用本测试文件在 compile_commands.json 里的编译命令, 把源文件换成场景文件, 这样头文件路径和宏都与工程一致
    std::string WriteScenario(const std::string& fileName, const char* source) {
        const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ccino_recursive_emission";
        std::filesystem::create_directories(dir);
        const std::filesystem::path sourcePath = dir / (fileName + ".cpp");
        std::ofstream(sourcePath, std::ios::binary) << source;

        std::ifstream ifs(CCINO_COMPILE_COMMAND_JSON_PATH);
        if (!ifs.is_open()) return {};
        nlohmann::json filtered = nlohmann::json::array();
        for (auto& entry : nlohmann::json::parse(ifs)) {
            std::string file = entry.value("file", "");
            std::replace(file.begin(), file.end(), '\\', '/');
            if (file.find("test_recursive_function_emission.cpp") == std::string::npos) continue;

            std::string command = entry["command"].get<std::string>();
            const std::string original = entry["file"].get<std::string>();
            std::string originalSlash = original;
            std::replace(originalSlash.begin(), originalSlash.end(), '\\', '/');
            std::string originalBackslash = original;
            std::replace(originalBackslash.begin(), originalBackslash.end(), '/', '\\');
            const std::string replacement = sourcePath.generic_string();
            ReplaceAll(command, originalSlash, replacement);
            ReplaceAll(command, originalBackslash, replacement);
            ReplaceAll(command, "-std:c++latest", "-std:c++20 -D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH");
            entry["command"] = command;
            entry["file"] = replacement;
            filtered.push_back(entry);
            break;
        }
        if (filtered.empty()) return {};

        const std::filesystem::path dbPath = dir / (fileName + "_compile_commands.json");
        std::ofstream(dbPath, std::ios::binary) << filtered.dump(2);
        return dbPath.generic_string();
    }

    const Ccino::ShaderIR* FindPass(const std::vector<Ccino::ShaderIR>& irs, const std::string& name) {
        for (const auto& ir : irs) {
            if (ir.passName == name) return &ir;
        }
        return nullptr;
    }

    size_t CountOccurrences(const std::string& text, const std::string& needle) {
        size_t count = 0;
        for (size_t pos = text.find(needle); pos != std::string::npos; pos = text.find(needle, pos + 1)) {
            ++count;
        }
        return count;
    }

    void ExpectCompiles(const Ccino::ShaderIR& ir) {
        EXPECT_NO_THROW(Ccino::GLSLCompiler::CompileToSpv(ir.passName, ir.cs, Ccino::ShaderType::Compute))
            << "Pass " << ir.passName << " GLSL:\n" << ir.cs;
    }

    class RecursiveFunctionEmissionTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            const std::string db = WriteScenario("recursive_scenarios", kScenarioSource);
            ASSERT_FALSE(db.empty()) << "compile command for test_recursive_function_emission.cpp not found";
            s_irs = Ccino::CppParser::ParseFilePassShaders(db);
        }
        static std::vector<Ccino::ShaderIR> s_irs;
    };
    std::vector<Ccino::ShaderIR> RecursiveFunctionEmissionTest::s_irs;
}  // namespace

TEST_F(RecursiveFunctionEmissionTest, MultiLevelChainIsCollected) {
    const auto* ir = FindPass(s_irs, "RecChainPass");
    ASSERT_NE(ir, nullptr);
    const size_t leaf = ir->cs.find("float LeafC_float (");
    const size_t mid = ir->cs.find("float MidB_float (");
    const size_t top = ir->cs.find("float TopA_float (");
    ASSERT_NE(leaf, std::string::npos) << ir->cs;
    ASSERT_NE(mid, std::string::npos) << ir->cs;
    ASSERT_NE(top, std::string::npos) << ir->cs;
    EXPECT_LT(leaf, mid);
    EXPECT_LT(mid, top);
    ExpectCompiles(*ir);
}

TEST_F(RecursiveFunctionEmissionTest, DiamondLeafEmittedOnce) {
    const auto* ir = FindPass(s_irs, "RecDiamondPass");
    ASSERT_NE(ir, nullptr);
    EXPECT_EQ(CountOccurrences(ir->cs, "float DiamondLeaf_float ("), 1u) << ir->cs;
    EXPECT_NE(ir->cs.find("float DiamondLeft_float ("), std::string::npos) << ir->cs;
    EXPECT_NE(ir->cs.find("float DiamondRight_float ("), std::string::npos) << ir->cs;
    ExpectCompiles(*ir);
}

TEST_F(RecursiveFunctionEmissionTest, MemberCallKeepsOuterSelf) {
    const auto* ir = FindPass(s_irs, "RecMemberPass");
    ASSERT_NE(ir, nullptr);
    EXPECT_NE(ir->cs.find("self.mass"), std::string::npos) << ir->cs;
    EXPECT_EQ(ir->cs.find("pc.uniformData.mass"), std::string::npos) << ir->cs;
    ExpectCompiles(*ir);
}

TEST_F(RecursiveFunctionEmissionTest, StructOnlyUsedInNestedHelperIsEmitted) {
    const auto* ir = FindPass(s_irs, "RecNestedStructPass");
    ASSERT_NE(ir, nullptr);
    EXPECT_NE(ir->cs.find("float InnerWithStruct_float ("), std::string::npos) << ir->cs;
    EXPECT_NE(ir->cs.find("struct LocalPair{"), std::string::npos) << ir->cs;
    ExpectCompiles(*ir);
}

TEST_F(RecursiveFunctionEmissionTest, NestedStructTreeInNestedHelperIsEmitted) {
    const auto* ir = FindPass(s_irs, "RecNestedStructTreePass");
    ASSERT_NE(ir, nullptr);
    const size_t leaf = ir->cs.find("struct TreeLeaf{");
    const size_t mid = ir->cs.find("struct TreeMid{");
    const size_t box = ir->cs.find("struct Box_TreeLeaf{");
    const size_t top = ir->cs.find("struct TreeTop{");
    ASSERT_NE(leaf, std::string::npos) << ir->cs;
    ASSERT_NE(mid, std::string::npos) << ir->cs;
    ASSERT_NE(box, std::string::npos) << ir->cs;
    ASSERT_NE(top, std::string::npos) << ir->cs;
    EXPECT_LT(leaf, mid);
    EXPECT_LT(leaf, box);
    EXPECT_LT(mid, top);
    EXPECT_LT(box, top);
    EXPECT_EQ(CountOccurrences(ir->cs, "struct TreeLeaf{"), 1u) << ir->cs;
    EXPECT_NE(ir->cs.find("TreeLeaf extra[2];"), std::string::npos) << ir->cs;
    EXPECT_NE(ir->cs.find("TreeLeaf arr[2];"), std::string::npos) << ir->cs;
    ExpectCompiles(*ir);
}

TEST_F(RecursiveFunctionEmissionTest, NestedStructInBufferElementIsEmitted) {
    const auto* ir = FindPass(s_irs, "RecNestedBufferPass");
    ASSERT_NE(ir, nullptr);
    const size_t leaf = ir->cs.find("struct TreeLeaf{");
    const size_t mid = ir->cs.find("struct TreeMid{");
    const size_t elem = ir->cs.find("buffer NestedElem{");
    ASSERT_NE(leaf, std::string::npos) << ir->cs;
    ASSERT_NE(mid, std::string::npos) << ir->cs;
    ASSERT_NE(elem, std::string::npos) << ir->cs;
    EXPECT_LT(leaf, mid);
    EXPECT_LT(mid, elem);
    ExpectCompiles(*ir);
}

//生成器检测到递归后停止展开, 函数只生成一次; GLSL 不允许递归, 交给 glslang 拒绝
TEST(RecursiveFunctionEmissionSelfTest, SelfRecursionTerminatesAndIsRejected) {
    const std::string db = WriteScenario("self_recursion", kSelfRecursionSource);
    ASSERT_FALSE(db.empty());
    const std::vector<Ccino::ShaderIR> irs = Ccino::CppParser::ParseFilePassShaders(db);
    const auto* ir = FindPass(irs, "RecSelfPass");
    ASSERT_NE(ir, nullptr);
    EXPECT_EQ(CountOccurrences(ir->cs, "int Factorial_int ("), 1u) << ir->cs;
    EXPECT_ANY_THROW(Ccino::GLSLCompiler::CompileToSpv(ir->passName, ir->cs, Ccino::ShaderType::Compute)) << ir->cs;
}
