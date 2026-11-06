#include "RHI/Context.h"
#include "TestContext.h"
#include "RHI/DescriptorSlot.h"
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>
#include <glm/glm.hpp>
#include <gtest/gtest.h>

namespace {
    using Ccino::RHI::UniformDataType;

    VkShaderModule LoadShaderModule(VkDevice device, const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            throw std::runtime_error("Failed to open shader: " + path.string());
        }
        const size_t size = static_cast<size_t>(file.tellg());
        std::vector<uint32_t> code(size / sizeof(uint32_t));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(size));

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = size;
        createInfo.pCode = code.data();

        VkShaderModule shaderModule = VK_NULL_HANDLE;
        VULKAN_CHECK(vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule));
        return shaderModule;
    }

    //和bind_param_test.comp 里Slot 结构体的成员顺序一致
    const Ccino::RHI::DescriptorSlotLayout kLayout = {
        {"result", UniformDataType::Buffer},
        {"f", UniformDataType::Float},
        {"i", UniformDataType::Int},
        {"u", UniformDataType::UInt},
        {"b", UniformDataType::Bool},
        {"v2", UniformDataType::Vec2},
        {"v3", UniformDataType::Vec3},
        {"afterV3", UniformDataType::Float},
        {"iv4", UniformDataType::IVec4},
        {"bv3", UniformDataType::BVec3},
        {"uv2", UniformDataType::UVec2},
        {"m3", UniformDataType::Mat3},
        {"m32", UniformDataType::Mat3x2},
        {"m23", UniformDataType::Mat2x3},
        {"m4", UniformDataType::Mat4},
        {"tail", UniformDataType::Float},
    };

    uint32_t Bits(float value) {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    //每个元素都不一样, 列或行写错位置时能看出来
    template<int C, int R>
    glm::mat<C, R, float> MakeMatrix(float base) {
        glm::mat<C, R, float> m(0.0f);
        for (int c = 0; c < C; ++c) {
            for (int r = 0; r < R; ++r) {
                m[c][r] = base + static_cast<float>(c * 10 + r);
            }
        }
        return m;
    }

    template<int C, int R>
    void AppendMatrix(std::vector<uint32_t>& out, const glm::mat<C, R, float>& m) {
        for (int c = 0; c < C; ++c) {
            for (int r = 0; r < R; ++r) {
                out.push_back(Bits(m[c][r]));
            }
        }
    }
}

//期望值来自glslang 编译bind_param_test.comp (scalar 布局) 后的反射结果
TEST(DescriptorSlot, Std140OffsetsMatchShader) {
    uint32_t size = 0;
    const auto offsets = Ccino::RHI::DescriptorSlotBinding::GetOffsetsTable(kLayout, size);

    const std::pair<const char*, uint32_t> expected[] = {
        {"result", 0}, {"f", 8}, {"i", 12}, {"u", 16}, {"b", 20}, {"v2", 24}, {"v3", 32}, {"afterV3", 44},
        {"iv4", 48}, {"bv3", 64}, {"uv2", 76}, {"m3", 84}, {"m32", 120}, {"m23", 144}, {"m4", 168}, {"tail", 232},
    };
    ASSERT_EQ(offsets.size(), std::size(expected));
    for (const auto& [name, offset] : expected) {
        ASSERT_TRUE(offsets.contains(name)) << name;
        EXPECT_EQ(offsets.at(name).offset, offset) << name;
    }
    EXPECT_EQ(size, 240u); //最后一个成员结束于236, 按16 字节补齐

    const Ccino::RHI::DescriptorSlotLayout duplicated = {{"a", UniformDataType::Float}, {"a", UniformDataType::Int}};
    EXPECT_THROW(Ccino::RHI::DescriptorSlotBinding::GetOffsetsTable(duplicated, size), std::runtime_error);
}

//每种常量经BindParam 写入, Shader 读出后按分量写回, CPU 逐个比对
TEST(DescriptorSlot, BindParam) {
    Ccino::RHI::Context& context = GetTestContext();

    VkDevice device = context.GetCore().GetVkDevice();
    const std::filesystem::path shaderPath = std::filesystem::path(__FILE__).parent_path() / "Shaders" / "bind_param_test.comp.spv";
    VkShaderModule shaderModule = LoadShaderModule(device, shaderPath);
    RID pipeline = context.CreateComputePipeline("BindParamTest", shaderModule);
    vkDestroyShaderModule(device, shaderModule, nullptr);
    ASSERT_NE(context.GetComputePipeline(pipeline), VK_NULL_HANDLE);

    Ccino::RHI::DescriptorSlot slot;
    slot.Init(&context);

    constexpr uint32_t kResultCount = 64;
    const RID result = context.CreateBuffer({kResultCount * sizeof(uint32_t), VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT, Ccino::RHI::HostAccess::ReadWrite,true});

    const float f = 1.5f;
    const int32_t i = -7;
    const uint32_t u = 42;
    const bool b = true;
    const glm::vec2 v2(2.5f, -3.25f);
    const glm::vec3 v3(4.0f, 5.0f, 6.0f);
    const float afterV3 = 7.75f;
    const glm::ivec4 iv4(-1, 2, -3, 4);
    const glm::bvec3 bv3(true, false, true);
    const glm::uvec2 uv2(100u, 200u);
    const glm::mat3 m3 = MakeMatrix<3, 3>(100.0f);
    const glm::mat3x2 m32 = MakeMatrix<3, 2>(200.0f);
    const glm::mat2x3 m23 = MakeMatrix<2, 3>(300.0f);
    const glm::mat4 m4 = MakeMatrix<4, 4>(400.0f);
    const float tail = 99.0f;

    {
        Ccino::RHI::DescriptorSlotBinding binding = slot.CreateSlotBinding(kLayout);
        binding.BindBuffer("result", result)
            .BindParam("f", f).BindParam("i", i).BindParam("u", u).BindParam("b", b)
            .BindParam("v2", v2).BindParam("v3", v3).BindParam("afterV3", afterV3)
            .BindParam("iv4", iv4).BindParam("bv3", bv3).BindParam("uv2", uv2)
            .BindParam("m3", m3).BindParam("m32", m32).BindParam("m23", m23).BindParam("m4", m4)
            .BindParam("tail", tail);

        //类型或名字不对时抛异常, 不修改已经写入的值
        EXPECT_THROW(binding.BindParam("f", 1.0), std::runtime_error); //double 写到Float
        EXPECT_THROW(binding.BindParam("m3", glm::mat4(1.0f)), std::runtime_error);
        EXPECT_THROW(binding.BindParam("missing", 1.0f), std::runtime_error);
        EXPECT_THROW(binding.BindBuffer("f", result), std::runtime_error);

        context.ExecuteCommand([&](Ccino::RHI::CommandContext::CommandBuffer& commandBuffer) {
            VkCommandBuffer cmd = commandBuffer.commandBuffer;
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, context.GetComputePipeline(pipeline));
            slot.BindDescriptorSet(cmd);
            binding.Push(cmd);
            vkCmdDispatch(cmd, 1, 1, 1);

            VkMemoryBarrier2 barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;

            VkDependencyInfo dependency{};
            dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependency.memoryBarrierCount = 1;
            dependency.pMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(cmd, &dependency);
        }, CommandQueueType::COMPUTE);
        VULKAN_CHECK(vkQueueWaitIdle(context.GetCore().GetVkQueue(CommandQueueType::COMPUTE)));
    }

    //和Shader 中的写出顺序一致
    std::vector<uint32_t> expected;
    expected.push_back(Bits(f));
    expected.push_back(static_cast<uint32_t>(i));
    expected.push_back(u);
    expected.push_back(b ? 1u : 0u);
    for (int c = 0; c < 2; ++c) expected.push_back(Bits(v2[c]));
    for (int c = 0; c < 3; ++c) expected.push_back(Bits(v3[c]));
    expected.push_back(Bits(afterV3));
    for (int c = 0; c < 4; ++c) expected.push_back(static_cast<uint32_t>(iv4[c]));
    for (int c = 0; c < 3; ++c) expected.push_back(bv3[c] ? 1u : 0u);
    for (int c = 0; c < 2; ++c) expected.push_back(uv2[c]);
    AppendMatrix(expected, m3);
    AppendMatrix(expected, m32);
    AppendMatrix(expected, m23);
    AppendMatrix(expected, m4);
    expected.push_back(Bits(tail));
    ASSERT_LE(expected.size(), kResultCount);

    auto* mapped = static_cast<const uint32_t*>(context.MapBuffer(result));
    ASSERT_NE(mapped, nullptr);
    for (size_t k = 0; k < expected.size(); ++k) {
        EXPECT_EQ(mapped[k], expected[k]) << "component " << k;
    }
    context.UnmapBuffer(result);

    context.DestroyComputePipeline(pipeline);
    context.DestroyBuffer(result);
    slot.Destroy();
}
