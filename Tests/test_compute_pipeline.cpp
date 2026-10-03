#include "TestContext.h"
#include "RHI/DescriptorSlot.h"
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <gtest/gtest.h>

namespace {
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

    struct Params {
        uint32_t multiplier = 0;
        uint32_t padding[3] = {};
    };
}

TEST(ComputePipeline, Dispatch) {
    Ccino::RHI::Context& context = GetTestContext();

    //测试用数据结构
    std::vector<uint32_t> data(64);
    for (uint32_t i = 0; i < data.size(); ++i) {
        data[i] = i * 3 + 1;
    }

    Ccino::RHI::DescriptorSlot descriptorSlot;
    descriptorSlot.Init(&context);

    //两个Pass 共用一个布局和管线, 各自绑定不同的UBO 和SSBO
    const uint32_t size = static_cast<uint32_t>(data.size() * sizeof(uint32_t));
    Params paramsData[2] = {{2}, {3}};
    RID params[2];
    RID outputs[2];
    for (int i = 0; i < 2; ++i) {
        params[i] = context.CreateUniformBuffer(sizeof(Params), &paramsData[i]);
        outputs[i] = context.CreateBuffer({size, VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT, true});
        void* mapped = context.MapBuffer(outputs[i]);
        ASSERT_NE(mapped, nullptr);
        std::memcpy(mapped, data.data(), size);
        context.UnmapBuffer(outputs[i]);
    }

    //和Shader 里Slot 结构体的成员顺序一致
    const Ccino::RHI::DescriptorSlotLayout layout = {
        {"params", {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER}},
        {"data", {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}},
    };

    VkDevice device = context.GetCore().GetVkDevice();
    const std::filesystem::path shaderPath = std::filesystem::path(__FILE__).parent_path() / "Shaders" / "compute_pipeline_test.comp.spv";
    VkShaderModule shaderModule = LoadShaderModule(device, shaderPath);
    RID pipeline = context.CreateComputePipeline("ComputePipelineTest", shaderModule);
    vkDestroyShaderModule(device, shaderModule, nullptr);
    ASSERT_NE(context.GetComputePipeline(pipeline), VK_NULL_HANDLE);

    {
        Ccino::RHI::DescriptorSlotBinding first = descriptorSlot.CreateSlotBinding(layout);
        first.BindBuffer("params", params[0]).BindBuffer("data", outputs[0]);
        Ccino::RHI::DescriptorSlotBinding second = descriptorSlot.CreateSlotBinding(layout);
        second.BindBuffer("params", params[1]).BindBuffer("data", outputs[1]);

        context.ExecuteCommand([&](Ccino::RHI::CommandContext::CommandBuffer& commandBuffer) {
            VkCommandBuffer cmd = commandBuffer.commandBuffer;
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, context.GetComputePipeline(pipeline));
            descriptorSlot.BindHeaps(cmd);

            first.Push(cmd);
            vkCmdDispatch(cmd, 8, 1, 1);
            second.Push(cmd);
            vkCmdDispatch(cmd, 8, 1, 1);

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

    for (int i = 0; i < 2; ++i) {
        auto* mapped = static_cast<uint32_t*>(context.MapBuffer(outputs[i]));
        ASSERT_NE(mapped, nullptr);
        for (uint32_t idx = 0; idx < data.size(); ++idx) {
            EXPECT_EQ(mapped[idx], idx * paramsData[i].multiplier) << "pass " << i << ", index " << idx;
        }
        context.UnmapBuffer(outputs[i]);
    }

    context.DestroyComputePipeline(pipeline);
    for (int i = 0; i < 2; ++i) {
        descriptorSlot.ReleaseBuffer(outputs[i]);
        descriptorSlot.ReleaseBuffer(params[i]);
        context.DestroyBuffer(outputs[i]);
        context.DestroyBuffer(params[i]);
    }
    descriptorSlot.Destroy();
}
