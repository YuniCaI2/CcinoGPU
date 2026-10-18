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
    constexpr uint32_t kElementCount = 64;

    struct Params {
        uint32_t multiplier = 0;
        uint32_t padding[3] = {};
    };

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

    RID CreateUniform(Ccino::RHI::Context& context, uint32_t multiplier) {
        Params params{multiplier};
        return context.CreateUniformBuffer(sizeof(Params), &params);
    }

    RID CreateStorage(Ccino::RHI::Context& context) {
        return context.CreateBuffer({kElementCount * sizeof(uint32_t), VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT, true});
    }

    void ExpectMultiplied(Ccino::RHI::Context& context, RID buffer, uint32_t multiplier, const char* what) {
        auto* mapped = static_cast<uint32_t*>(context.MapBuffer(buffer));
        if (mapped == nullptr) {
            ADD_FAILURE() << what << ": failed to map output";
            return;
        }
        for (uint32_t i = 0; i < kElementCount; ++i) {
            if (mapped[i] != i * multiplier) {
                EXPECT_EQ(mapped[i], i * multiplier) << what << ", index " << i;
                break;
            }
        }
        context.UnmapBuffer(buffer);
    }

    void RecordDispatch(VkCommandBuffer cmd, VkPipeline pipeline, Ccino::RHI::DescriptorSlot& slot, Ccino::RHI::DescriptorSlotBinding& binding) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        slot.BindDescriptorSet(cmd);
        binding.Push(cmd);
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
    }

    void ReleaseAndDestroy(Ccino::RHI::Context& context, Ccino::RHI::DescriptorSlot& slot, const std::vector<RID>& buffers) {
        for (RID rid : buffers) {
            context.DestroyBuffer(rid);
        }
        slot.Destroy();
    }

    //和compute_pipeline_test.comp 里Slot 结构体的成员顺序一致, Buffer 字段存的是BDA
    const Ccino::RHI::DescriptorSlotLayout kLayout = {
        {"params", Ccino::RHI::UniformDataType::Buffer},
        {"data", Ccino::RHI::UniformDataType::Buffer},
    };

    // 两个 Pass 同时引用同一个 UBO。其中一个改绑之后，另一个必须仍然读到原来的 multiplier。
    // 每个 Binding 有自己的 UniformBuffer，Bind 只修改自己的 CPU 副本；Buffer 通过 BDA 访问，不存在共享的槽位。
    // Update 之后再创建一个绑定新 UBO 的 Binding，也不能影响已有的 Binding。
    void TestSharedReferenceSurvivesOverwrite(Ccino::RHI::Context& context, VkPipeline pipeline) {
        Ccino::RHI::DescriptorSlot slot;
        slot.Init(&context);

        constexpr uint32_t kShared = 5;
        constexpr uint32_t kReplacement = 4;
        constexpr uint32_t kIntruder = 9;

        const RID shared = CreateUniform(context, kShared);
        const RID replacement = CreateUniform(context, kReplacement);
        const RID intruderParams = CreateUniform(context, kIntruder);
        const RID sharedOut = CreateStorage(context);
        const RID intruderOut = CreateStorage(context);
        const std::vector<RID> buffers = {shared, replacement, intruderParams, sharedOut, intruderOut};

        {
            Ccino::RHI::DescriptorSlotBinding pass1 = slot.CreateSlotBinding(kLayout);
            Ccino::RHI::DescriptorSlotBinding pass2 = slot.CreateSlotBinding(kLayout);
            pass1.BindBuffer("params", shared).BindBuffer("data", sharedOut);
            pass2.BindBuffer("params", shared).BindBuffer("data", sharedOut);

            pass1.BindBuffer("params", replacement);
            slot.Update();

            Ccino::RHI::DescriptorSlotBinding reuse = slot.CreateSlotBinding(kLayout);
            reuse.BindBuffer("params", intruderParams).BindBuffer("data", intruderOut);

            context.ExecuteCommand([&](Ccino::RHI::CommandContext::CommandBuffer& commandBuffer) {
                RecordDispatch(commandBuffer.commandBuffer, pipeline, slot, reuse);
                RecordDispatch(commandBuffer.commandBuffer, pipeline, slot, pass2);
            }, CommandQueueType::COMPUTE);
            VULKAN_CHECK(vkQueueWaitIdle(context.GetCore().GetVkQueue(CommandQueueType::COMPUTE)));
            ExpectMultiplied(context, intruderOut, kIntruder, "new buffer descriptor was not visible");
            ExpectMultiplied(context, sharedOut, kShared, "shared buffer overwritten while another pass still references it");
        }

        ReleaseAndDestroy(context, slot, buffers);
    }

    // 命令录好之后再改绑，已经录制的命令必须仍然读到改绑之前的 UBO。
    // Bind 只修改 CPU 副本，下一次 Push 才写入 UniformBuffer，所以录制时推送的 UniformBuffer 内容不会变。
    void TestSoleOverwriteStaysPending(Ccino::RHI::Context& context, VkPipeline pipeline) {
        Ccino::RHI::DescriptorSlot slot;
        slot.Init(&context);

        constexpr uint32_t kOriginal = 2;
        constexpr uint32_t kReplacement = 3;
        constexpr uint32_t kIntruder = 9;

        const RID original = CreateUniform(context, kOriginal);
        const RID replacement = CreateUniform(context, kReplacement);
        const RID intruderParams = CreateUniform(context, kIntruder);
        const RID out = CreateStorage(context);
        const RID intruderOut = CreateStorage(context);
        const std::vector<RID> buffers = {original, replacement, intruderParams, out, intruderOut};

        {
            Ccino::RHI::DescriptorSlotBinding binding = slot.CreateSlotBinding(kLayout);
            binding.BindBuffer("params", original).BindBuffer("data", out);

            Ccino::RHI::CommandContext::CommandBuffer& commandBuffer = context.GetCommandBuffer(CommandQueueType::COMPUTE);
            context.StartRecording(commandBuffer);
            RecordDispatch(commandBuffer.commandBuffer, pipeline, slot, binding);
            context.EndRecording(commandBuffer);

            binding.BindBuffer("params", replacement);

            Ccino::RHI::DescriptorSlotBinding reuse = slot.CreateSlotBinding(kLayout);
            reuse.BindBuffer("params", intruderParams).BindBuffer("data", intruderOut);

            context.SubmitCommandBuffer(CommandQueueType::COMPUTE, {commandBuffer});
            VULKAN_CHECK(vkQueueWaitIdle(context.GetCore().GetVkQueue(CommandQueueType::COMPUTE)));
            context.ExecuteCommand([&](Ccino::RHI::CommandContext::CommandBuffer& recorded) {
                RecordDispatch(recorded.commandBuffer, pipeline, slot, reuse);
            }, CommandQueueType::COMPUTE);
            VULKAN_CHECK(vkQueueWaitIdle(context.GetCore().GetVkQueue(CommandQueueType::COMPUTE)));
            ExpectMultiplied(context, intruderOut, kIntruder, "new buffer descriptor was not visible");
            ExpectMultiplied(context, out, kOriginal, "recorded command saw a Bind made after it was recorded");
        }

        ReleaseAndDestroy(context, slot, buffers);
    }

    // 原来验证的是 Heap 槽位在 Update 之后被回收、并被下一个 Bind 复用；BDA 方案里 Buffer 不占槽位，没有可回收的东西。
    // 对应的行为改为：改绑并 Update 之后，旧命令仍读到旧 UBO；等它执行完，再次 Push 时这一帧的 UniformBuffer
    // 被复用并写入改绑后的内容（飞行帧为 1），新的 Dispatch 读到新 UBO，另一个 Binding 不受影响。
    void TestSoleOverwriteRecycledAfterUpdate(Ccino::RHI::Context& context, VkPipeline pipeline) {
        Ccino::RHI::DescriptorSlot slot;
        slot.Init(&context);

        constexpr uint32_t kOriginal = 2;
        constexpr uint32_t kReplacement = 3;
        constexpr uint32_t kIntruder = 7;

        const RID original = CreateUniform(context, kOriginal);
        const RID replacement = CreateUniform(context, kReplacement);
        const RID intruderParams = CreateUniform(context, kIntruder);
        const RID out = CreateStorage(context);
        const RID intruderOut = CreateStorage(context);
        const std::vector<RID> buffers = {original, replacement, intruderParams, out, intruderOut};

        {
            Ccino::RHI::DescriptorSlotBinding binding = slot.CreateSlotBinding(kLayout);
            binding.BindBuffer("params", original).BindBuffer("data", out);

            Ccino::RHI::CommandContext::CommandBuffer& commandBuffer = context.GetCommandBuffer(CommandQueueType::COMPUTE);
            context.StartRecording(commandBuffer);
            RecordDispatch(commandBuffer.commandBuffer, pipeline, slot, binding);
            context.EndRecording(commandBuffer);

            binding.BindBuffer("params", replacement);
            slot.Update();

            Ccino::RHI::DescriptorSlotBinding reuse = slot.CreateSlotBinding(kLayout);
            reuse.BindBuffer("params", intruderParams).BindBuffer("data", intruderOut);

            context.SubmitCommandBuffer(CommandQueueType::COMPUTE, {commandBuffer});
            VULKAN_CHECK(vkQueueWaitIdle(context.GetCore().GetVkQueue(CommandQueueType::COMPUTE)));
            ExpectMultiplied(context, out, kOriginal, "recorded command saw a Bind made after it was recorded");

            context.ExecuteCommand([&](Ccino::RHI::CommandContext::CommandBuffer& recorded) {
                RecordDispatch(recorded.commandBuffer, pipeline, slot, binding);
                RecordDispatch(recorded.commandBuffer, pipeline, slot, reuse);
            }, CommandQueueType::COMPUTE);
            VULKAN_CHECK(vkQueueWaitIdle(context.GetCore().GetVkQueue(CommandQueueType::COMPUTE)));
            ExpectMultiplied(context, intruderOut, kIntruder, "new binding was not visible");
            ExpectMultiplied(context, out, kReplacement, "uniform buffer was not rewritten by the next Push");
        }

        ReleaseAndDestroy(context, slot, buffers);
    }
}

TEST(DescriptorSlot, BindOverwriteReferenceCount) {
    Ccino::RHI::Context& context = GetTestContext();
    ASSERT_EQ(context.GetFlightFrameCount(), 1u);

    VkDevice device = context.GetCore().GetVkDevice();
    const std::filesystem::path shaderPath = std::filesystem::path(__FILE__).parent_path() / "Shaders" / "compute_pipeline_test.comp.spv";
    VkShaderModule shaderModule = LoadShaderModule(device, shaderPath);
    RID pipeline = context.CreateComputePipeline("DescriptorSlotRefCount", shaderModule);
    vkDestroyShaderModule(device, shaderModule, nullptr);
    ASSERT_NE(context.GetComputePipeline(pipeline), VK_NULL_HANDLE);

    TestSharedReferenceSurvivesOverwrite(context, context.GetComputePipeline(pipeline));
    TestSoleOverwriteStaysPending(context, context.GetComputePipeline(pipeline));
    TestSoleOverwriteRecycledAfterUpdate(context, context.GetComputePipeline(pipeline));

    context.DestroyComputePipeline(pipeline);
}
