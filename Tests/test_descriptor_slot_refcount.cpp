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
        slot.BindHeaps(cmd);
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
            slot.ReleaseBuffer(rid);
            context.DestroyBuffer(rid);
        }
        slot.Destroy();
    }

    // 两个 Pass 同时引用同一个 UBO。其中一个覆盖掉之后，这个 UBO 不能进析构队列。
    // 飞行帧为 1，Update 会把待释放桶倒进空闲队列；接着再绑一个新 UBO，拿去占刚空出来的槽。
    // 还握着旧 UBO 的那个 Pass 必须仍然读到原来的 multiplier。
    void TestSharedReferenceSurvivesOverwrite(Ccino::RHI::Context& context, VkPipeline pipeline) {
        Ccino::RHI::DescriptorSlot slot;
        slot.Init(&context);

        const Ccino::RHI::DescriptorSlotLayout layout = {
            {"params", {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER}},
            {"data", {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}},
        };

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
            Ccino::RHI::DescriptorSlotBinding pass1 = slot.CreateSlotBinding(layout);
            Ccino::RHI::DescriptorSlotBinding pass2 = slot.CreateSlotBinding(layout);
            pass1.BindBuffer("params", shared).BindBuffer("data", sharedOut);
            pass2.BindBuffer("params", shared).BindBuffer("data", sharedOut);

            pass1.BindBuffer("params", replacement);
            slot.Update();

            Ccino::RHI::DescriptorSlotBinding reuse = slot.CreateSlotBinding(layout);
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

    // 只有一个引用时，覆盖应把旧槽放进当前飞行帧的待释放桶，而不是立刻给下一个 Bind 用。
    // 命令在覆盖前录好，块里仍是旧索引。新 UBO 若提前复用了这个槽，dispatch 会读到新的 multiplier。
    void TestSoleOverwriteStaysPending(Ccino::RHI::Context& context, VkPipeline pipeline) {
        Ccino::RHI::DescriptorSlot slot;
        slot.Init(&context);

        const Ccino::RHI::DescriptorSlotLayout layout = {
            {"params", {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER}},
            {"data", {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}},
        };

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
            Ccino::RHI::DescriptorSlotBinding binding = slot.CreateSlotBinding(layout);
            binding.BindBuffer("params", original).BindBuffer("data", out);

            Ccino::RHI::CommandContext::CommandBuffer& commandBuffer = context.GetCommandBuffer(CommandQueueType::COMPUTE);
            context.StartRecording(commandBuffer);
            RecordDispatch(commandBuffer.commandBuffer, pipeline, slot, binding);
            context.EndRecording(commandBuffer);

            binding.BindBuffer("params", replacement);

            Ccino::RHI::DescriptorSlotBinding reuse = slot.CreateSlotBinding(layout);
            reuse.BindBuffer("params", intruderParams).BindBuffer("data", intruderOut);

            context.SubmitCommandBuffer(CommandQueueType::COMPUTE, {commandBuffer});
            VULKAN_CHECK(vkQueueWaitIdle(context.GetCore().GetVkQueue(CommandQueueType::COMPUTE)));
            context.ExecuteCommand([&](Ccino::RHI::CommandContext::CommandBuffer& recorded) {
                RecordDispatch(recorded.commandBuffer, pipeline, slot, reuse);
            }, CommandQueueType::COMPUTE);
            VULKAN_CHECK(vkQueueWaitIdle(context.GetCore().GetVkQueue(CommandQueueType::COMPUTE)));
            ExpectMultiplied(context, intruderOut, kIntruder, "new buffer descriptor was not visible");
            ExpectMultiplied(context, out, kOriginal, "sole buffer was reused before Update");
        }

        ReleaseAndDestroy(context, slot, buffers);
    }

    // 同一个覆盖，Update 一次后（飞行帧为 1）旧槽必须从待释放桶进入空闲队列，并被下一个 Bind 写上。
    void TestSoleOverwriteRecycledAfterUpdate(Ccino::RHI::Context& context, VkPipeline pipeline) {
        Ccino::RHI::DescriptorSlot slot;
        slot.Init(&context);

        const Ccino::RHI::DescriptorSlotLayout layout = {
            {"params", {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER}},
            {"data", {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}},
        };

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
            Ccino::RHI::DescriptorSlotBinding binding = slot.CreateSlotBinding(layout);
            binding.BindBuffer("params", original).BindBuffer("data", out);

            Ccino::RHI::CommandContext::CommandBuffer& commandBuffer = context.GetCommandBuffer(CommandQueueType::COMPUTE);
            context.StartRecording(commandBuffer);
            RecordDispatch(commandBuffer.commandBuffer, pipeline, slot, binding);
            context.EndRecording(commandBuffer);

            binding.BindBuffer("params", replacement);
            slot.Update();

            Ccino::RHI::DescriptorSlotBinding reuse = slot.CreateSlotBinding(layout);
            reuse.BindBuffer("params", intruderParams).BindBuffer("data", intruderOut);

            context.SubmitCommandBuffer(CommandQueueType::COMPUTE, {commandBuffer});
            VULKAN_CHECK(vkQueueWaitIdle(context.GetCore().GetVkQueue(CommandQueueType::COMPUTE)));
            context.ExecuteCommand([&](Ccino::RHI::CommandContext::CommandBuffer& recorded) {
                RecordDispatch(recorded.commandBuffer, pipeline, slot, reuse);
            }, CommandQueueType::COMPUTE);
            VULKAN_CHECK(vkQueueWaitIdle(context.GetCore().GetVkQueue(CommandQueueType::COMPUTE)));
            ExpectMultiplied(context, intruderOut, kIntruder, "new buffer descriptor was not visible");
            ExpectMultiplied(context, out, kIntruder, "sole buffer did not enter the release queue");
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
