#include "ShaderStruct/UniformData.h"
#include "RHI/DescriptorSlot.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
    using Ccino::DescriptorSlotLayout;
    using Ccino::GetUniformTypeInfo;
    using Ccino::UniformData;
    using Ccino::UniformDataType;
    using Ccino::UniformOffset;
    using Ccino::UniformTypeInfo;
    using nlohmann::json;

    const std::pair<UniformDataType, const char*> kAllUniformDataTypes[] = {
        // Resource
        {UniformDataType::Buffer,       "Buffer"},
        {UniformDataType::SampledImage, "SampledImage"},
        {UniformDataType::StorageImage, "StorageImage"},
        {UniformDataType::Sampler,      "Sampler"},

        // Scalar
        {UniformDataType::Bool,   "Bool"},
        {UniformDataType::Int,    "Int"},
        {UniformDataType::UInt,   "UInt"},
        {UniformDataType::UInt64, "UInt64"},
        {UniformDataType::Float,  "Float"},
        {UniformDataType::Double, "Double"},

        // Vector
        {UniformDataType::BVec2, "BVec2"},
        {UniformDataType::BVec3, "BVec3"},
        {UniformDataType::BVec4, "BVec4"},
        {UniformDataType::IVec2, "IVec2"},
        {UniformDataType::IVec3, "IVec3"},
        {UniformDataType::IVec4, "IVec4"},
        {UniformDataType::UVec2, "UVec2"},
        {UniformDataType::UVec3, "UVec3"},
        {UniformDataType::UVec4, "UVec4"},
        {UniformDataType::Vec2,  "Vec2"},
        {UniformDataType::Vec3,  "Vec3"},
        {UniformDataType::Vec4,  "Vec4"},
        {UniformDataType::DVec2, "DVec2"},
        {UniformDataType::DVec3, "DVec3"},
        {UniformDataType::DVec4, "DVec4"},

        // Float Matrix
        {UniformDataType::Mat2,   "Mat2"},
        {UniformDataType::Mat3,   "Mat3"},
        {UniformDataType::Mat4,   "Mat4"},
        {UniformDataType::Mat2x3, "Mat2x3"},
        {UniformDataType::Mat2x4, "Mat2x4"},
        {UniformDataType::Mat3x2, "Mat3x2"},
        {UniformDataType::Mat3x4, "Mat3x4"},
        {UniformDataType::Mat4x2, "Mat4x2"},
        {UniformDataType::Mat4x3, "Mat4x3"},

        // Double Matrix
        {UniformDataType::DMat2,   "DMat2"},
        {UniformDataType::DMat3,   "DMat3"},
        {UniformDataType::DMat4,   "DMat4"},
        {UniformDataType::DMat2x3, "DMat2x3"},
        {UniformDataType::DMat2x4, "DMat2x4"},
        {UniformDataType::DMat3x2, "DMat3x2"},
        {UniformDataType::DMat3x4, "DMat3x4"},
        {UniformDataType::DMat4x2, "DMat4x2"},
        {UniformDataType::DMat4x3, "DMat4x3"},
    };
}

TEST(UniformDataJson, AllUniformDataTypesRoundTrip) {
    for (const auto& [type, expectedName] : kAllUniformDataTypes) {
        const json j = type;
        ASSERT_TRUE(j.is_string()) << expectedName;
        EXPECT_EQ(j.get<std::string>(), expectedName);

        const json reparsed = json::parse(j.dump());
        EXPECT_EQ(reparsed.get<UniformDataType>(), type) << expectedName;
    }

    EXPECT_THROW(json("InvalidType").get<UniformDataType>(), json::out_of_range);
    EXPECT_THROW(json(static_cast<UniformDataType>(999)), json::out_of_range);
}

TEST(UniformDataJson, UniformDataRoundTrip) {
    const UniformData original{"uViewProj", UniformDataType::Mat4};
    const json j = original;

    ASSERT_TRUE(j.is_object());
    EXPECT_EQ(j.at("name"), "uViewProj");
    EXPECT_EQ(j.at("dataType"), "Mat4");

    const UniformData deserialized = json::parse(j.dump()).get<UniformData>();
    EXPECT_EQ(deserialized, original);

    const json missingField = {{"name", "onlyName"}};
    EXPECT_THROW(missingField.get<UniformData>(), json::out_of_range);
}

TEST(UniformDataJson, DescriptorSlotLayoutRoundTripAndStd140Consistency) {
    const DescriptorSlotLayout emptyLayout{};
    EXPECT_EQ(json::parse(json(emptyLayout).dump()).get<DescriptorSlotLayout>(), emptyLayout);

    const DescriptorSlotLayout layout = {
        {"result",   UniformDataType::Buffer},
        {"albedo",   UniformDataType::SampledImage},
        {"outImage", UniformDataType::StorageImage},
        {"linear",   UniformDataType::Sampler},
        {"exposure", UniformDataType::Float},
        {"lightDir", UniformDataType::Vec3},
        {"viewProj", UniformDataType::Mat4},
        {"dmat",     UniformDataType::DMat3x2},
    };

    const std::string dumped = json(layout).dump(2);
    const DescriptorSlotLayout restored = json::parse(dumped).get<DescriptorSlotLayout>();
    EXPECT_EQ(restored, layout);

    uint32_t originalSize = 0;
    uint32_t restoredSize = 0;
    const auto originalOffsets = Ccino::RHI::DescriptorSlotBinding::GetOffsetsTable(layout, originalSize);
    const auto restoredOffsets = Ccino::RHI::DescriptorSlotBinding::GetOffsetsTable(restored, restoredSize);
    EXPECT_EQ(restoredSize, originalSize);
    EXPECT_EQ(restoredOffsets, originalOffsets);
}

TEST(UniformDataJson, UniformOffsetAndUniformTypeInfoRoundTrip) {
    std::unordered_map<std::string, UniformOffset> offsets = {
        {"result", {0u, UniformDataType::Buffer}},
        {"f",      {8u, UniformDataType::Float}},
        {"m3",     {96u, UniformDataType::Mat3}},
    };

    const auto restoredOffsets = json::parse(json(offsets).dump()).get<std::unordered_map<std::string, UniformOffset>>();
    EXPECT_EQ(restoredOffsets, offsets);

    for (const auto& [type, name] : kAllUniformDataTypes) {
        const UniformTypeInfo info = GetUniformTypeInfo(type);
        const json j = info;

        EXPECT_EQ(j.at("size"), info.size) << name;
        EXPECT_EQ(j.at("alignment"), info.alignment) << name;

        const UniformTypeInfo restoredInfo = json::parse(j.dump()).get<UniformTypeInfo>();
        EXPECT_EQ(restoredInfo, info) << name;
    }
}
