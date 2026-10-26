#include "ShaderStruct/ShaderIR.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
    using Ccino::DescriptorSlotLayout;
    using Ccino::ShaderFunction;
    using Ccino::ShaderIR;
    using Ccino::ShaderIRRegistry;
    using Ccino::ShaderPropertyBinding;
    using Ccino::ShaderPropertyType;
    using Ccino::ShaderType;
    using Ccino::ShaderUniformDataBinding;
    using Ccino::ThreadGroupSize;
    using Ccino::UniformDataType;
    using nlohmann::json;

    const std::pair<ShaderPropertyType, const char*> kShaderPropertyTypes[] = {
        {ShaderPropertyType::ReadOnlyStorageBuffer,  "ReadOnlyStorageBuffer"},
        {ShaderPropertyType::WriteOnlyStorageBuffer, "WriteOnlyStorageBuffer"},
        {ShaderPropertyType::ReadWriteStorageBuffer, "ReadWriteStorageBuffer"},
    };

    bool SameBinding(const ShaderUniformDataBinding& lhs, const ShaderUniformDataBinding& rhs) {
        return lhs.name == rhs.name && lhs.offset == rhs.offset && lhs.uniformDataType == rhs.uniformDataType;
    }

    bool SameShaderIR(const ShaderIR& lhs, const ShaderIR& rhs) {
        if (lhs.passName != rhs.passName || lhs.shaderType != rhs.shaderType || lhs.threadGroupSize != rhs.threadGroupSize) {
            return false;
        }
        if (lhs.descriptorSlotLayout != rhs.descriptorSlotLayout || lhs.glsl != rhs.glsl ||
            lhs.spvPath != rhs.spvPath || lhs.size != rhs.size) {
            return false;
        }
        if (lhs.uniformDataBindings.size() != rhs.uniformDataBindings.size()) {
            return false;
        }
        for (std::size_t i = 0; i < lhs.uniformDataBindings.size(); ++i) {
            if (!SameBinding(lhs.uniformDataBindings[i], rhs.uniformDataBindings[i])) {
                return false;
            }
        }
        return true;
    }

    ShaderIR MakeComputeIR() {
        ShaderIR ir;
        ir.passName = "ClearBuffer";
        ir.shaderType = ShaderType::Compute;
        ir.threadGroupSize = ThreadGroupSize{64, 1, 1};
        ir.descriptorSlotLayout = DescriptorSlotLayout{
            {"result", UniformDataType::Buffer},
            {"exposure", UniformDataType::Float},
        };
        ir.uniformDataBindings = {
            {"exposure", 16u, UniformDataType::Float},
            {"tint", 32u, UniformDataType::Vec4},
        };
        ir.glsl = "layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;\n"
                  "void main() {\n"
                  "    // \"ClearBuffer\"\n"
                  "}\n";
        ir.spvPath = ".CcinoGPU/Shaders/ClearBuffer.spv";
        ir.size = static_cast<uint32_t>(ir.glsl.size());
        return ir;
    }

    json RoundTrip(const json& value) {
        return json::parse(value.dump(4));
    }

    std::vector<ShaderIR> LoadGeneratedShaderIRs(const std::filesystem::path& shaderDir) {
        std::vector<ShaderIR> shaderIRs;
        for (const auto& entry : std::filesystem::directory_iterator(shaderDir)) {
            const std::filesystem::path& shaderPath = entry.path();
            if (!entry.is_regular_file() || shaderPath.extension() != ".json") {
                continue;
            }
            std::ifstream shaderFile(shaderPath);
            json shaderJson = json::parse(shaderFile);
            if (!shaderJson.is_object() || !shaderJson.contains("passName")) {
                continue;
            }
            shaderIRs.push_back(shaderJson.get<ShaderIR>());
        }
        return shaderIRs;
    }

    class ShaderIRRegistryTest : public ::testing::Test {
    protected:
        void SetUp() override {
            auto& registry = ShaderIRRegistry::Instance();
            for (const auto& [name, ir] : registry.GetAll()) {
                (void)name;
                saved.push_back(ir);
            }
            registry.Clear();
        }

        void TearDown() override {
            auto& registry = ShaderIRRegistry::Instance();
            registry.Clear();
            registry.RegisterAll(std::move(saved));
        }

        std::vector<ShaderIR> saved;
    };
}

TEST(ShaderIRJson, ShaderPropertyTypeRoundTrip) {
    for (const auto& [type, expectedName] : kShaderPropertyTypes) {
        const json value = type;
        ASSERT_TRUE(value.is_string()) << expectedName;
        EXPECT_EQ(value.get<std::string>(), expectedName);

        const ShaderPropertyType restored = RoundTrip(value).get<ShaderPropertyType>();
        EXPECT_EQ(restored, type) << expectedName;
    }

    EXPECT_THROW(json("Sampler").get<ShaderPropertyType>(), json::out_of_range);
    EXPECT_THROW(json(static_cast<ShaderPropertyType>(999)), json::out_of_range);
}

TEST(ShaderIRJson, ShaderPropertyBindingRoundTrip) {
    const ShaderPropertyBinding original{"particles", ShaderPropertyType::ReadWriteStorageBuffer};
    const json value = original;

    EXPECT_EQ(value.at("name"), "particles");
    EXPECT_EQ(value.at("propertyType"), "ReadWriteStorageBuffer");

    const ShaderPropertyBinding restored = RoundTrip(value).get<ShaderPropertyBinding>();
    EXPECT_EQ(restored.name, original.name);
    EXPECT_EQ(restored.propertyType, original.propertyType);

    const json missingField = {{"name", "particles"}};
    EXPECT_THROW(missingField.get<ShaderPropertyBinding>(), json::out_of_range);
}

TEST(ShaderIRJson, ShaderUniformDataBindingRoundTrip) {
    const ShaderUniformDataBinding original{"exposure", 16u, UniformDataType::Float};
    const json value = original;

    EXPECT_EQ(value.at("name"), "exposure");
    EXPECT_EQ(value.at("offset"), 16);
    EXPECT_EQ(value.at("uniformDataType"), "Float");

    const ShaderUniformDataBinding restored = RoundTrip(value).get<ShaderUniformDataBinding>();
    EXPECT_TRUE(SameBinding(restored, original));
}

TEST(ShaderIRJson, ShaderFunctionRoundTrip) {
    const ShaderFunction original{"Luminance", "float Luminance(vec3 color) { return dot(color, vec3(0.3)); }\n"};
    const json value = original;

    EXPECT_EQ(value.at("name"), original.name);
    EXPECT_EQ(value.at("glsl"), original.glsl);

    const ShaderFunction restored = RoundTrip(value).get<ShaderFunction>();
    EXPECT_EQ(restored.name, original.name);
    EXPECT_EQ(restored.glsl, original.glsl);
}

TEST(ShaderIRJson, ShaderIRRoundTrip) {
    const ShaderIR original = MakeComputeIR();
    const json value = original;

    EXPECT_EQ(value.at("passName"), "ClearBuffer");
    EXPECT_EQ(value.at("shaderType"), "Compute");
    EXPECT_EQ(value.at("threadGroupSize").at("x"), 64);
    EXPECT_EQ(value.at("threadGroupSize").at("y"), 1);
    EXPECT_EQ(value.at("threadGroupSize").at("z"), 1);
    ASSERT_EQ(value.at("descriptorSlotLayout").size(), 2u);
    EXPECT_EQ(value.at("descriptorSlotLayout").at(0).at("name"), "result");
    EXPECT_EQ(value.at("descriptorSlotLayout").at(0).at("dataType"), "Buffer");
    ASSERT_EQ(value.at("uniformDataBindings").size(), 2u);
    EXPECT_EQ(value.at("uniformDataBindings").at(1).at("name"), "tint");
    EXPECT_EQ(value.at("uniformDataBindings").at(1).at("uniformDataType"), "Vec4");
    EXPECT_EQ(value.at("glsl"), original.glsl);
    EXPECT_EQ(value.at("size"), original.size);

    const ShaderIR restored = RoundTrip(value).get<ShaderIR>();
    EXPECT_TRUE(SameShaderIR(restored, original));
}

TEST(ShaderIRJson, ShaderIRRejectsIncompleteJson) {
    const json missingGlsl = {
        {"passName", "ClearBuffer"},
        {"shaderType", "Compute"},
        {"threadGroupSize", {{"x", 8}, {"y", 1}, {"z", 1}}},
        {"descriptorSlotLayout", json::array()},
        {"uniformDataBindings", json::array()},
        {"size", 0},
    };
    EXPECT_THROW(missingGlsl.get<ShaderIR>(), json::out_of_range);

    json wrongPassName = json(MakeComputeIR());
    wrongPassName["passName"] = 1;
    EXPECT_THROW(wrongPassName.get<ShaderIR>(), json::type_error);
    EXPECT_THROW(json::array().get<ShaderIR>(), json::type_error);
}

TEST(ShaderIRJson, GeneratedDirectorySkipsNonShaderJson) {
    const ShaderIR compute = MakeComputeIR();
    ShaderIR vertex = compute;
    vertex.passName = "Blit";
    vertex.shaderType = ShaderType::Vertex;
    vertex.threadGroupSize = {};
    vertex.glsl = "void main() {}\n";
    vertex.size = static_cast<uint32_t>(vertex.glsl.size());

    struct TempShaderDir {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
            ("ccino_shader_ir_" + std::to_string(std::random_device{}()));
        TempShaderDir() { std::filesystem::create_directories(path); }
        ~TempShaderDir() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } shaderDir;

    const auto writeJson = [&](const std::string& fileName, const json& value) {
        std::ofstream output(shaderDir.path / fileName, std::ios::binary | std::ios::trunc);
        EXPECT_TRUE(output.is_open()) << fileName;
        output << value.dump(4) << '\n';
    };
    writeJson(compute.passName + ".json", compute);
    writeJson(vertex.passName + ".json", vertex);
    writeJson("filtered_compile_commands.json", json::array({{{"file", "main.cpp"}}}));
    writeJson("notes.json", {{"comment", "not a shader"}});
    {
        std::ofstream glsl(shaderDir.path / "HugeHeader.glsl", std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(glsl.is_open());
        glsl << "float Luminance(vec3 color) { return color.r; }\n";
    }

    const std::vector<ShaderIR> loaded = LoadGeneratedShaderIRs(shaderDir.path);

    ASSERT_EQ(loaded.size(), 2u);
    const bool firstIsCompute = loaded[0].passName == compute.passName;
    EXPECT_TRUE(SameShaderIR(loaded[firstIsCompute ? 0 : 1], compute));
    EXPECT_TRUE(SameShaderIR(loaded[firstIsCompute ? 1 : 0], vertex));
}

TEST_F(ShaderIRRegistryTest, RegisterFindAndReplace) {
    auto& registry = ShaderIRRegistry::Instance();
    const ShaderIR original = MakeComputeIR();
    registry.Register(original);

    EXPECT_TRUE(registry.Contains(original.passName));
    EXPECT_EQ(registry.Find("Missing"), nullptr);
    const ShaderIR* found = registry.Find(original.passName);
    ASSERT_NE(found, nullptr);
    EXPECT_TRUE(SameShaderIR(*found, original));

    ShaderIR replaced = original;
    replaced.glsl = "void main() {}\n";
    replaced.size = static_cast<uint32_t>(replaced.glsl.size());
    registry.Register(replaced);
    EXPECT_TRUE(SameShaderIR(registry[original.passName], replaced));
    EXPECT_EQ(registry.GetAll().size(), 1u);
}

TEST_F(ShaderIRRegistryTest, RegisterAllClearAndMapRoundTrip) {
    auto& registry = ShaderIRRegistry::Instance();
    ShaderIR first = MakeComputeIR();
    ShaderIR second = first;
    second.passName = "Blit";
    second.shaderType = ShaderType::Fragment;

    std::vector<ShaderIR> batch;
    batch.push_back(first);
    batch.push_back(second);
    registry.RegisterAll(std::move(batch));

    EXPECT_TRUE(registry.Contains(first.passName));
    EXPECT_TRUE(registry.Contains(second.passName));
    EXPECT_EQ(registry.GetAll().size(), 2u);

    const auto restored = RoundTrip(registry.GetAll()).get<std::unordered_map<std::string, ShaderIR>>();
    ASSERT_EQ(restored.size(), 2u);
    EXPECT_TRUE(SameShaderIR(restored.at(first.passName), first));
    EXPECT_TRUE(SameShaderIR(restored.at(second.passName), second));

    registry.Clear();
    EXPECT_FALSE(registry.Contains(first.passName));
    EXPECT_TRUE(registry.GetAll().empty());
}
