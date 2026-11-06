#include <gtest/gtest.h>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "Core/CcinoType.h"
#include "RG/RenderGraph.h"
#include "CodeGenerator/CppParser.h"
#include "GLSLCompiler/GLSLCompiler.h"
#include "ShaderStruct/ShaderIR.h"

namespace
{
    constexpr uint32_t kSpirvMagicNumber = 0x07230203u;

    // 测试用全局结构体
    struct LightSample {
        Ccino::Vec3 radiance;
        float weight;
    };

    struct MaterialParam {
        int roughnessLevel;
        float metallic;
    };

    struct OutputPixel {
        Ccino::Vec3 color;
        float alpha;
    };

    struct WorkItem {
        int a;
        int b;
    };

    struct BufferData {
        int value;
    };

    // 测试用全局函数
    float ComputeAttenuation(int level, float baseWeight) {
        int clamped = Ccino::Clamp(level, 1, 8);
        return baseWeight * 0.5f + float(clamped);
    }

    Ccino::Vec3 EvaluateLighting(LightSample lightSample, MaterialParam mat) {
        float atten = ComputeAttenuation(mat.roughnessLevel, lightSample.weight);
        return lightSample.radiance * (atten * mat.metallic);
    }

    int CombineWorkItem(WorkItem item, int scale) {
        return (item.a + item.b) * scale;
    }

    // 真实 C++ RenderGraph 构建代码
    void BuildTestGlobalFuncAndStructPasses() {
        Ccino::RGBuilder builder;
        RID outBuf = builder.CreateTransientBuffer({.size = 1024});
        RID buf0   = builder.CreateTransientBuffer({.size = 512});
        RID buf1   = builder.CreateTransientBuffer({.size = 512});

        builder.AddPass<"DeferredLightingPass">([&](Ccino::RGBuilder& rg, RID& passRID) {
            auto outPixels = rg.Write<OutputPixel>(outBuf, passRID);
            Ccino::Vec3 sunRadiance(1.0f, 0.9f, 0.8f);
            float exposure = 1.5f;

            rg.AddCS<Ccino::ThreadGroupSize{64, 1, 1}>([=]() {
                uint32_t idx = Ccino::ComputeGlobalID.x;

                LightSample s{sunRadiance, exposure};
                MaterialParam m;
                m.roughnessLevel = 3;
                m.metallic = 0.8f;

                float atten = ComputeAttenuation(m.roughnessLevel, s.weight);
                Ccino::Vec3 lit = EvaluateLighting(s, m);

                outPixels[idx].color = lit;
                outPixels[idx].alpha = atten;
            }, Ccino::WorkGroupSize{1, 1, 1});
        });

        builder.AddPass<"CombinePassOne">([&](Ccino::RGBuilder& rg, RID& passRID) {
            auto out0 = rg.Write<BufferData>(buf0, passRID);
            int factor = 2;

            rg.AddCS<Ccino::ThreadGroupSize{32, 1, 1}>([=]() {
                WorkItem w1{1, 2};
                WorkItem w2{3, 4};
                out0[Ccino::ComputeGlobalID.x].value = CombineWorkItem(w1, factor) + CombineWorkItem(w2, factor);
            }, Ccino::WorkGroupSize{1, 1, 1});
        }).AddPass<"CombinePassTwo">([&](Ccino::RGBuilder& rg, RID& passRID) {
            auto in0  = rg.Read<BufferData>(buf0, passRID);
            auto out1 = rg.Write<BufferData>(buf1, passRID);
            int bias = 10;

            auto cs = [=]() {
                WorkItem w{in0[Ccino::ComputeGlobalID.x].value, bias};
                out1[Ccino::ComputeGlobalID.x].value = CombineWorkItem(w, 3);
            };
            rg.AddCS<Ccino::ThreadGroupSize{16, 1, 1}>(cs, Ccino::WorkGroupSize{1, 1, 1});
        });
    }

    // 从 CMake 生成的 CCINO_COMPILE_COMMAND_JSON_PATH 加载并剔除第三方库/无关文件，转存到 CCINO_SHADER_DIR 下
    std::string FilterAndWriteCompileCommands() {
        std::ifstream ifs(CCINO_COMPILE_COMMAND_JSON_PATH);
        if (!ifs.is_open()) {
            return {};
        }

        nlohmann::json allCommands = nlohmann::json::parse(ifs);
        nlohmann::json filteredCommands = nlohmann::json::array();

        for (auto& entry : allCommands) {
            if (!entry.contains("file")) continue;
            std::string file = entry["file"].get<std::string>();

            // 剔除第三方库等无关文件，仅保留当前测试源文件
            if (file.find("ThirdPart") != std::string::npos) continue;
            if (file.find("test_global_func_and_struct_spv.cpp") == std::string::npos) continue;

            // Clang 19 兼容 VS 2026 (MSVC 14.51) STL 头文件
            if (entry.contains("command")) {
                std::string cmd = entry["command"].get<std::string>();
                size_t pos = cmd.find("-std:c++latest");
                if (pos != std::string::npos) {
                    cmd.replace(pos, std::string("-std:c++latest").size(),
                                "-std:c++20 -D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH");
                }
                entry["command"] = std::move(cmd);
            }

            filteredCommands.push_back(std::move(entry));
        }

        std::filesystem::path outDir(CCINO_SHADER_DIR);
        std::filesystem::create_directories(outDir);
        std::filesystem::path outJsonPath = outDir / "filtered_compile_commands.json";

        std::ofstream ofs(outJsonPath, std::ios::binary);
        ofs << filteredCommands.dump(2);
        ofs.close();

        return outJsonPath.generic_string();
    }
}  // namespace

TEST(GlobalFuncAndStructSpvTest, ParseFromProjectCompileCommandsAndCompileToSpv) {
    BuildTestGlobalFuncAndStructPasses();

    const std::string filteredCompileCommandsPath = FilterAndWriteCompileCommands();
    ASSERT_FALSE(filteredCompileCommandsPath.empty());

    std::string globalFunctions;
    std::vector<Ccino::ShaderIR> shaderIRs =
        Ccino::CppParser::ParseFilePassShaders(filteredCompileCommandsPath, &globalFunctions);

    ASSERT_EQ(shaderIRs.size(), 3u);

    const std::string& deferredGlsl = shaderIRs[0].cs;
    const std::string& combineOneGlsl = shaderIRs[1].cs;

    // 1. 验证结构体定义已在对应 Pass 中按需生成且无重复
    const std::string expectedLightSampleStruct =
        "struct LightSample{\n"
        "vec3 radiance;\n"
        "float weight;\n"
        "};\n";
    const std::string expectedMaterialParamStruct =
        "struct MaterialParam{\n"
        "int roughnessLevel;\n"
        "float metallic;\n"
        "};\n";
    const std::string expectedWorkItemStruct =
        "struct WorkItem{\n"
        "int a;\n"
        "int b;\n"
        "};\n";

    const size_t lightStructPos = deferredGlsl.find(expectedLightSampleStruct);
    const size_t matStructPos   = deferredGlsl.find(expectedMaterialParamStruct);
    const size_t workStructPos  = combineOneGlsl.find(expectedWorkItemStruct);
    ASSERT_NE(lightStructPos, std::string::npos) << "deferredGlsl:\n" << deferredGlsl;
    ASSERT_NE(matStructPos, std::string::npos) << "deferredGlsl:\n" << deferredGlsl;
    ASSERT_NE(workStructPos, std::string::npos) << "combineOneGlsl:\n" << combineOneGlsl;
    EXPECT_EQ(combineOneGlsl.find("struct WorkItem{", workStructPos + 1), std::string::npos)
        << "Duplicate struct WorkItem definition in combineOneGlsl:\n" << combineOneGlsl;

    // 2. 验证全局函数定义已展平生成且无重复，内置函数包装 Clamp 已转为 clamp
    const size_t attenFuncPos   = deferredGlsl.find("float ComputeAttenuation_int_float (int level, float baseWeight)");
    const size_t evalFuncPos    = deferredGlsl.find("vec3 EvaluateLighting_LightSample_MaterialParam (LightSample lightSample, MaterialParam mat)");
    const size_t combineFuncPos = combineOneGlsl.find("int CombineWorkItem_WorkItem_int (WorkItem item, int scale)");
    ASSERT_NE(attenFuncPos, std::string::npos) << "deferredGlsl:\n" << deferredGlsl;
    ASSERT_NE(evalFuncPos, std::string::npos) << "deferredGlsl:\n" << deferredGlsl;
    ASSERT_NE(combineFuncPos, std::string::npos) << "combineOneGlsl:\n" << combineOneGlsl;
    EXPECT_EQ(combineOneGlsl.find("int CombineWorkItem_WorkItem_int (", combineFuncPos + 1), std::string::npos)
        << "Duplicate CombineWorkItem definition in combineOneGlsl:\n" << combineOneGlsl;
    EXPECT_EQ(deferredGlsl.find("Clamp("), std::string::npos);

    // 3. 验证拓扑排序：结构体和被依赖子函数排在调用者之前
    EXPECT_LT(lightStructPos, evalFuncPos);
    EXPECT_LT(matStructPos, evalFuncPos);
    EXPECT_LT(attenFuncPos, evalFuncPos);
    EXPECT_LT(workStructPos, combineFuncPos);

    // 4. 验证每个 Pass 自包含生成的 GLSL 可以直接编译为 SPIR-V
    for (auto& shaderIR : shaderIRs) {
        const auto it = shaderIR.cs.find("in;");
        ASSERT_NE(it, std::string::npos);

        std::vector<uint32_t> spv;
        EXPECT_NO_THROW({
            spv = Ccino::GLSLCompiler::CompileToSpv(shaderIR.passName, shaderIR.cs, Ccino::ShaderType::Compute);
        }) << "Pass " << shaderIR.passName << " failed to compile GLSL:\n" << shaderIR.cs;

        ASSERT_GT(spv.size(), 5u);
        EXPECT_EQ(spv[0], kSpirvMagicNumber);
    }
}
