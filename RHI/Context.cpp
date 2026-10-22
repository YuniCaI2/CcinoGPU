#include "RHI/Context.h"
#include "RHI/ComputePipeline.h"
#include "RHI/DescriptorSlot.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace Ccino::RHI {
    namespace {
        bool IsHostWriteBuffer(VkBufferUsageFlags2 usage) {
            if ((usage & VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT) != 0) {
                return false;
            }
            return (usage & VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT) != 0 ||
                (usage & VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT) != 0;
        }
    }

    Context::Context() = default;

    void Context::Init(const RHISettings& settings) {
        m_core.Init(settings);
        m_commandContext.Init(m_core.GetVkDevice(), {
            m_core.GetGraphicsQueueFamilyIndex(),
            m_core.GetComputeQueueFamilyIndex(),
            m_core.GetTransferQueueFamilyIndex()
        });
        CreateGlobalTimelineSemaphore();
        DescriptorSlot::InitGlobal(m_core); //全局唯一的DescriptorSetLayout / PipelineLayout / DescriptorPool
        m_flightFrameCount = settings.flightFrameCount; //飞行帧数用来处理资源和同步的分配

        m_bufferDeletionQueue.resize(m_flightFrameCount); //初始化队列
        m_descriptorSlot.Init(this);        
    }

    void Context::Tick() {
        std::vector<RID> expired;
        {
            std::lock_guard lock(m_deletionMutex);
            m_currentFrameIndex = (m_currentFrameIndex + 1) % m_flightFrameCount;
            expired.swap(m_bufferDeletionQueue[m_currentFrameIndex]); //取出后在锁外销毁, 避免销毁过程中持锁
        }
        for (RID rid : expired) {
            DestroyResource(rid);
        }
    }

    void Context::Destroy() {
        VkDevice device = m_core.GetVkDevice();
        if (device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device);
        }

        m_buffers.ForEach([&](VulkanBuffer& buffer) {
            if (buffer.mappedPtr != nullptr && !buffer.persistentlyMapped) {
                vmaUnmapMemory(m_core.GetVmaAllocator(), buffer.allocation);
                buffer.mappedPtr = nullptr;
            }
            if (buffer.buffer != VK_NULL_HANDLE) {
                vmaDestroyBuffer(m_core.GetVmaAllocator(), buffer.buffer, buffer.allocation);
                buffer.buffer = VK_NULL_HANDLE;
                buffer.allocation = VK_NULL_HANDLE;
            }
        });

        if (device != VK_NULL_HANDLE) {
            m_semaphores.ForEach([&](VkSemaphore& semaphore) {
                if (semaphore != VK_NULL_HANDLE) {
                    vkDestroySemaphore(device, semaphore, nullptr);
                    semaphore = VK_NULL_HANDLE;
                }
            });
        }

        DescriptorSlot::DestroyGlobal();
        DestroyGlobalTimelineSemaphore();
        m_commandContext.Destroy();
        m_core.Destroy();
    }

    void Context::DeleteResource(RID rid) {
        //类型匹配 
        std::lock_guard lock(m_deletionMutex);
        m_bufferDeletionQueue[m_currentFrameIndex].push_back(rid);
    }

    RID Context::CreateBuffer(const BufferCreateInfo& createInfo) {
        const VkBufferUsageFlags2 usage = createInfo.usage | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT;
        const bool hostVisible = createInfo.hostVisible || IsHostWriteBuffer(usage);
        const bool hostRead = hostVisible && !IsHostWriteBuffer(usage);

        VkBufferCreateInfo bufferCreateInfo{};
        bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferCreateInfo.size = createInfo.size;
        bufferCreateInfo.usage = 0;
        bufferCreateInfo.flags = 0;

        VkBufferUsageFlags2CreateInfo bufferUsageCreateInfo{};
        bufferUsageCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_USAGE_FLAGS_2_CREATE_INFO;
        bufferUsageCreateInfo.usage = usage;
        bufferCreateInfo.pNext = &bufferUsageCreateInfo;

        uint32_t queueFamilies[] = {
            m_core.GetGraphicsQueueFamilyIndex(),
            m_core.GetComputeQueueFamilyIndex(),
            m_core.GetTransferQueueFamilyIndex()
        };
        std::vector<uint32_t> uniqueQueueFamilies;
        for (uint32_t family : queueFamilies) {
            if (std::find(uniqueQueueFamilies.begin(), uniqueQueueFamilies.end(), family) == uniqueQueueFamilies.end()) {
                uniqueQueueFamilies.push_back(family);
            }
        }
        if (uniqueQueueFamilies.size() > 1) {
            bufferCreateInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
            bufferCreateInfo.queueFamilyIndexCount = static_cast<uint32_t>(uniqueQueueFamilies.size());
            bufferCreateInfo.pQueueFamilyIndices = uniqueQueueFamilies.data();
        } else {
            bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        VmaAllocationCreateInfo allocCreateInfo{};
        if (hostVisible) {
            // Staging / Uniform 由 CPU 写入。Device-local 的初始数据走 Transfer 队列。
            allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            allocCreateInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
                (hostRead ? VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
                          : VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        } else {
            allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        }

        VulkanBuffer buffer{};
        VmaAllocationInfo allocInfo{};
        VULKAN_CHECK(vmaCreateBuffer(
            m_core.GetVmaAllocator(),
            &bufferCreateInfo,
            &allocCreateInfo,
            &buffer.buffer,
            &buffer.allocation,
            &allocInfo));

        buffer.usage = usage;
        buffer.size = createInfo.size;
        buffer.hostVisible = hostVisible;
        if ((allocCreateInfo.flags & VMA_ALLOCATION_CREATE_MAPPED_BIT) != 0 && allocInfo.pMappedData != nullptr) {
            buffer.mappedPtr = allocInfo.pMappedData;
            buffer.persistentlyMapped = true;
        }

        VkBufferDeviceAddressInfo addressInfo{};
        addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addressInfo.buffer = buffer.buffer;
        buffer.bda = vkGetBufferDeviceAddress(m_core.GetVkDevice(), &addressInfo);

        return m_buffers.Add(std::move(buffer));
    }

    bool Context::GetBufferUsage(RID rid, VkBufferUsageFlags2& usage) {
        VulkanBuffer* buffer = m_buffers.Get(rid);
        if (buffer == nullptr) {
            return false;
        }
        usage = buffer->usage;
        return true;
    }

    bool Context::GetImageDescriptorType(RID, VkDescriptorType&) {
        return false;
    }

    bool Context::GetSamplerDescriptorType(RID, VkDescriptorType&) {
        return false;
    }

    RID Context::CreateUniformBuffer(uint32_t size, const void* data) {
        RID rid = CreateBuffer(BufferCreateInfo{size, VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT});
        if (data == nullptr) {
            MapBuffer(rid);
        } else {
            void* ptr = MapBuffer(rid);
            if (ptr == nullptr) {
                throw std::runtime_error("Failed to map uniform buffer");
            }
            std::memcpy(ptr, data, size);
            UnmapBuffer(rid);
        }
        return rid;
    }

    RID Context::CreateTransferBuffer(uint32_t size, const void* data) {
        RID rid = CreateBuffer(BufferCreateInfo{size, VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT});
        if (data != nullptr && size > 0) {
            void* mapped = MapBuffer(rid);
            if (mapped == nullptr) {
                throw std::runtime_error("Failed to map transfer buffer");
            }
            std::memcpy(mapped, data, size);
            UnmapBuffer(rid);
        }
        return rid;
    }

    RID Context::CreateStorageBuffer(uint32_t size, const void* data) {
        RID rid = CreateBuffer(BufferCreateInfo{
            size,
            VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT | VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT
        });
        if (data != nullptr && size > 0) {
            UploadBuffer(rid, data, size);
        }
        return rid;
    }

    RID Context::CreateReadbackBuffer(uint32_t size) {
        BufferCreateInfo createInfo;
        createInfo.size = size;
        createInfo.usage = VK_BUFFER_USAGE_2_TRANSFER_DST_BIT;
        createInfo.hostVisible = true;
        return CreateBuffer(createInfo);
    }

    void Context::ReadbackBuffer(RID src, RID dst) {
        VulkanBuffer* source = m_buffers.Get(src);
        VulkanBuffer* destination = m_buffers.Get(dst);
        if (source == nullptr || destination == nullptr) {
            throw std::runtime_error("Readback buffer is invalid");
        }
        if (destination->size < source->size) {
            throw std::runtime_error("Readback buffer is smaller than the source");
        }

        const VkBuffer srcBuffer = source->buffer;
        const VkBuffer dstBuffer = destination->buffer;
        const uint32_t size = source->size;

        CommandContext::CommandBuffer& commandBuffer = GetCommandBuffer(CommandQueueType::TRANSFER);
        StartRecording(commandBuffer);
        VkBufferCopy region{};
        region.size = size;
        vkCmdCopyBuffer(commandBuffer.commandBuffer, srcBuffer, dstBuffer, 1, &region);

        VkBufferMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = dstBuffer;
        barrier.offset = 0;
        barrier.size = size;

        VkDependencyInfo dependency{};
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.bufferMemoryBarrierCount = 1;
        dependency.pBufferMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(commandBuffer.commandBuffer, &dependency);
        EndRecording(commandBuffer);

        const uint64_t value = SubmitCommandBufferTracked(CommandQueueType::TRANSFER, {commandBuffer}, 0, {}, {}, {});
        destination = m_buffers.Get(dst);
        if (destination == nullptr) {
            throw std::runtime_error("Readback buffer was destroyed");
        }
        destination->isAvailable = value;
    }

    void Context::UploadBuffer(RID rid, const void* data, uint32_t size) {
        if (data == nullptr || size == 0) {
            return;
        }
        if (m_buffers.Get(rid) == nullptr) {
            return;
        }

        CommandContext::CommandBuffer& commandBuffer = GetCommandBuffer(CommandQueueType::TRANSFER);
        StartRecording(commandBuffer);

        VulkanBuffer* destination = m_buffers.Get(rid);
        if (destination == nullptr) {
            throw std::runtime_error("Upload destination was destroyed");
        }
        ReleaseUploadStaging(*destination);

        RID stagingRid = CreateTransferBuffer(size, data);
        VulkanBuffer* staging = m_buffers.Get(stagingRid);
        destination = m_buffers.Get(rid);
        if (staging == nullptr || destination == nullptr) {
            DestroyBuffer(stagingRid);
            throw std::runtime_error("Failed to create staging buffer for upload");
        }

        const VkBuffer src = staging->buffer;
        const VkBuffer dst = destination->buffer;
        bool submitted = false;
        try {
            VkBufferCopy region{};
            region.size = size;
            vkCmdCopyBuffer(commandBuffer.commandBuffer, src, dst, 1, &region);
            EndRecording(commandBuffer);

            const uint64_t value = SubmitCommandBufferTracked(CommandQueueType::TRANSFER, {commandBuffer}, 0, {}, {}, {});
            submitted = true;

            destination = m_buffers.Get(rid);
            if (destination == nullptr) {
                throw std::runtime_error("Upload destination was destroyed");
            }
            destination->isAvailable = value;
            destination->uploadStaging = stagingRid;
        } catch (...) {
            if (!submitted) {
                DestroyBuffer(stagingRid);
            }
            throw;
        }
    }

    bool Context::IsBufferAvailable(RID rid) {
        VulkanBuffer* buffer = m_buffers.Get(rid);
        if (buffer == nullptr) {
            return false;
        }
        if (buffer->isAvailable == 0) {
            return true;
        }

        const bool available = IsGlobalTimelineSemaphoreAvailable(CommandQueueType::TRANSFER, buffer->isAvailable);
        if (available) {
            ReleaseUploadStaging(*buffer);
        }
        return available;
    }

    void* Context::MapBuffer(RID rid) {
        VulkanBuffer* buffer = m_buffers.Get(rid);
        if (buffer == nullptr || !buffer->hostVisible) {
            std::cerr << "Buffer is not a uniform buffer or transfer source buffer" << std::endl;
            return nullptr;
        }
        if (buffer->mappedPtr == nullptr) {
            void* mapped = nullptr;
            VULKAN_CHECK(vmaMapMemory(m_core.GetVmaAllocator(), buffer->allocation, &mapped));
            buffer->mappedPtr = mapped;
        }
        if (!IsHostWriteBuffer(buffer->usage)) {
            VULKAN_CHECK(vmaInvalidateAllocation(m_core.GetVmaAllocator(), buffer->allocation, 0, buffer->size));
        }
        return buffer->mappedPtr;
    }

    void Context::UnmapBuffer(RID rid) {
        VulkanBuffer* buffer = m_buffers.Get(rid);
        if (buffer == nullptr || !buffer->hostVisible) {
            std::cerr << "Buffer is not a uniform buffer or transfer source buffer" << std::endl;
            return;
        }

        if (IsHostWriteBuffer(buffer->usage)) {
            VULKAN_CHECK(vmaFlushAllocation(m_core.GetVmaAllocator(), buffer->allocation, 0, buffer->size));
        }
        if (!buffer->persistentlyMapped && buffer->mappedPtr != nullptr) {
            vmaUnmapMemory(m_core.GetVmaAllocator(), buffer->allocation);
            buffer->mappedPtr = nullptr;
        }
    }

    void Context::WaitForUploadStaging(RID rid) {
        VulkanBuffer* buffer = m_buffers.Get(rid);
        if (IsHostWriteBuffer(buffer->usage)) {
            return;
        }

        if (TimelineSemaphore* transferTimeline = GetTimeline(m_core.GetQueueFamilyIndex(CommandQueueType::TRANSFER));
            transferTimeline != nullptr) {
            if (buffer->isAvailable < transferTimeline->ReadValue()) {
                return;
            } else {
                transferTimeline->Wait(buffer->isAvailable);
            }
        } else {
            throw std::runtime_error("Transfer timeline is not available");
        }
    }

    void Context::ReleaseUploadStaging(VulkanBuffer& buffer) {
        RID staging = buffer.uploadStaging;
        buffer.uploadStaging = RID::Invalid();
        if (staging) {
            DestroyBuffer(staging);
        }
    }

    void Context::DestroyBuffer(RID rid) {
        VulkanBuffer* buffer = m_buffers.Get(rid);
        if (buffer == nullptr) {
            return;
        }

        if (buffer->isAvailable != 0) {
            if (TimelineSemaphore* timeline = GetTimeline(m_core.GetQueueFamilyIndex(CommandQueueType::TRANSFER))) {
                timeline->Wait(buffer->isAvailable);
            }
        }
        ReleaseUploadStaging(*buffer);

        if (buffer->mappedPtr != nullptr && !buffer->persistentlyMapped) {
            vmaUnmapMemory(m_core.GetVmaAllocator(), buffer->allocation);
            buffer->mappedPtr = nullptr;
        }
        if (buffer->buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_core.GetVmaAllocator(), buffer->buffer, buffer->allocation);
            buffer->buffer = VK_NULL_HANDLE;
            buffer->allocation = VK_NULL_HANDLE;
        }
        m_buffers.Remove(rid);
    }

    RID Context::CreateSemaphore() {
        VkSemaphoreCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkSemaphore semaphore = VK_NULL_HANDLE;
        VULKAN_CHECK(vkCreateSemaphore(m_core.GetVkDevice(), &createInfo, nullptr, &semaphore));
        return m_semaphores.Add(std::move(semaphore));
    }

    void Context::DestroySemaphore(RID rid) {
        VkSemaphore* semaphore = m_semaphores.Get(rid);
        if (semaphore == nullptr || *semaphore == VK_NULL_HANDLE) {
            return;
        }
        vkDestroySemaphore(m_core.GetVkDevice(), *semaphore, nullptr);
        *semaphore = VK_NULL_HANDLE;
        m_semaphores.Remove(rid);
    }

    void Context::CreateGlobalTimelineSemaphore() {
        DestroyGlobalTimelineSemaphore();

        const uint32_t familyCount = m_core.GetQueueFamilyCount();
        m_timelines.resize(familyCount);

        auto createForFamily = [&](uint32_t family) {
            if (family >= familyCount) {
                return;
            }
            if (m_timelines[family] == nullptr) {
                m_timelines[family] = std::make_unique<TimelineSemaphore>();
            }
            if (m_timelines[family]->GetSemaphore() != VK_NULL_HANDLE) {
                return;
            }
            m_timelines[family]->Create(m_core.GetVkDevice());
        };

        createForFamily(m_core.GetGraphicsQueueFamilyIndex());
        createForFamily(m_core.GetComputeQueueFamilyIndex());
        createForFamily(m_core.GetTransferQueueFamilyIndex());
    }

    void Context::DestroyGlobalTimelineSemaphore() {
        for (std::unique_ptr<TimelineSemaphore>& timeline : m_timelines) {
            if (timeline != nullptr) {
                timeline->Destroy();
            }
        }
        m_timelines.clear();
    }

    bool Context::IsGlobalTimelineSemaphoreAvailable(CommandQueueType queueType, uint64_t value) {
        TimelineSemaphore* timeline = GetTimeline(m_core.GetQueueFamilyIndex(queueType));
        if (timeline == nullptr) {
            return value == 0;
        }
        return timeline->IsAvailable(value);
    }

    TimelineSemaphore* Context::GetTimeline(uint32_t queueFamilyIndex) {
        if (queueFamilyIndex >= m_timelines.size() || m_timelines[queueFamilyIndex] == nullptr ||
            m_timelines[queueFamilyIndex]->GetSemaphore() == VK_NULL_HANDLE) {
            return nullptr;
        }
        return m_timelines[queueFamilyIndex].get();
    }

    CommandContext::CommandBuffer& Context::GetCommandBuffer(CommandQueueType queueType, uint32_t frameIndex) {
        return m_commandContext.GetCommandBuffer(m_core.GetQueueFamilyIndex(queueType), frameIndex);
    }

    void Context::StartRecording(CommandContext::CommandBuffer& commandBuffer) {
        TimelineSemaphore* timeline = GetTimeline(commandBuffer.queueFamilyIndex);
        const uint64_t pendingValue = timeline != nullptr ? timeline->ReadValue() : 0;
        if (timeline != nullptr) {
            timeline->Wait(pendingValue);
        }

        VULKAN_CHECK(vkResetCommandBuffer(commandBuffer.commandBuffer, 0));
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VULKAN_CHECK(vkBeginCommandBuffer(commandBuffer.commandBuffer, &beginInfo));
    }

    void Context::EndRecording(CommandContext::CommandBuffer& commandBuffer) {
        VULKAN_CHECK(vkEndCommandBuffer(commandBuffer.commandBuffer));
    }

    void Context::SubmitCommandBuffer(CommandQueueType queueType, const std::vector<CommandContext::CommandBuffer>& commandBuffers, uint64_t signalValue, const std::vector<RID>& waitSemaphores, const std::vector<RID>& signalSemaphores, const std::vector<VkPipelineStageFlags2> pipelineStageFlags) {
        SubmitCommandBufferTracked(queueType, commandBuffers, signalValue, waitSemaphores, signalSemaphores, pipelineStageFlags);
    }

    void Context::DestroyResource(RID rid) {
        if (rid.type == RID::GetIndex<VulkanBuffer>()) {
            DestroyBuffer(rid);
        } else if (rid.type == RID::GetIndex<VkSemaphore>()) {
            DestroySemaphore(rid);
        } else if (rid.type == RID::GetIndex<ComputePipeline>()) {
            DestroyComputePipeline(rid);
        } else {
            throw std::runtime_error("Invalid resource type");
        }
    }

    uint64_t Context::SubmitCommandBufferTracked(CommandQueueType queueType, const std::vector<CommandContext::CommandBuffer>& commandBuffers, uint64_t signalValue, const std::vector<RID>& waitSemaphores, const std::vector<RID>& signalSemaphores, const std::vector<VkPipelineStageFlags2>& pipelineStageFlags) {
        if (commandBuffers.empty()) {
            return 0;
        }

        const uint32_t queueFamilyIndex = m_core.GetQueueFamilyIndex(queueType);
        std::vector<VkCommandBufferSubmitInfo> commandBufferInfos;
        commandBufferInfos.reserve(commandBuffers.size());
        for (const CommandContext::CommandBuffer& commandBuffer : commandBuffers) {
            if (commandBuffer.queueFamilyIndex != queueFamilyIndex) {
                throw std::runtime_error("Command buffer does not belong to the submit queue");
            }
            VkCommandBufferSubmitInfo info{};
            info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
            info.commandBuffer = commandBuffer.commandBuffer;
            commandBufferInfos.push_back(info);
        }

        std::vector<VkSemaphoreSubmitInfo> userWaits;
        userWaits.reserve(waitSemaphores.size());
        for (size_t i = 0; i < waitSemaphores.size(); ++i) {
            VkSemaphore* semaphore = m_semaphores.Get(waitSemaphores[i]);
            if (semaphore == nullptr || *semaphore == VK_NULL_HANDLE) {
                throw std::runtime_error("Invalid wait semaphore");
            }
            VkSemaphoreSubmitInfo info{};
            info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            info.semaphore = *semaphore;
            info.stageMask = i < pipelineStageFlags.size() ? pipelineStageFlags[i] : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            userWaits.push_back(info);
        }

        std::vector<VkSemaphore> userSignals;
        userSignals.reserve(signalSemaphores.size());
        for (RID rid : signalSemaphores) {
            VkSemaphore* semaphore = m_semaphores.Get(rid);
            if (semaphore == nullptr || *semaphore == VK_NULL_HANDLE) {
                throw std::runtime_error("Invalid signal semaphore");
            }
            userSignals.push_back(*semaphore);
        }

        auto appendWait = [](std::vector<VkSemaphoreSubmitInfo>& waits, VkSemaphore semaphore, uint64_t value) {
            if (semaphore == VK_NULL_HANDLE || value == 0) {
                return;
            }
            for (VkSemaphoreSubmitInfo& wait : waits) {
                if (wait.semaphore == semaphore) {
                    if (value > wait.value) {
                        wait.value = value;
                    }
                    return;
                }
            }
            VkSemaphoreSubmitInfo info{};
            info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            info.semaphore = semaphore;
            info.value = value;
            info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            waits.push_back(info);
        };

        TimelineSemaphore* timeline = GetTimeline(queueFamilyIndex);
        if (timeline == nullptr) {
            throw std::runtime_error("Queue family has no timeline semaphore");
        }

        TimelineSemaphore::Signal signal = timeline->BeginSignal(signalValue);

        VkSemaphore transferSemaphore = VK_NULL_HANDLE;
        uint64_t transferValue = 0;
        if (TimelineSemaphore* transferTimeline = GetTimeline(m_core.GetQueueFamilyIndex(CommandQueueType::TRANSFER));
            transferTimeline != nullptr && transferTimeline != timeline) {
            transferSemaphore = transferTimeline->GetSemaphore();
            transferValue = transferTimeline->ReadValue();
        }
        //保证异步的上传队列上传完毕后再进行提交

        std::vector<VkSemaphoreSubmitInfo> waits = userWaits;
        appendWait(waits, timeline->GetSemaphore(), signal.previous);
        appendWait(waits, transferSemaphore, transferValue);

        std::vector<VkSemaphoreSubmitInfo> signals;
        signals.reserve(userSignals.size() + 1);
        for (VkSemaphore semaphore : userSignals) {
            VkSemaphoreSubmitInfo info{};
            info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            info.semaphore = semaphore;
            info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            signals.push_back(info);
        }
        VkSemaphoreSubmitInfo timelineInfo{};
        timelineInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        timelineInfo.semaphore = timeline->GetSemaphore();
        timelineInfo.value = signal.current;
        timelineInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        signals.push_back(timelineInfo);

        VkSubmitInfo2 submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submitInfo.waitSemaphoreInfoCount = static_cast<uint32_t>(waits.size());
        submitInfo.pWaitSemaphoreInfos = waits.empty() ? nullptr : waits.data();
        submitInfo.commandBufferInfoCount = static_cast<uint32_t>(commandBufferInfos.size());
        submitInfo.pCommandBufferInfos = commandBufferInfos.data();
        submitInfo.signalSemaphoreInfoCount = static_cast<uint32_t>(signals.size());
        submitInfo.pSignalSemaphoreInfos = signals.data();
        VULKAN_CHECK(vkQueueSubmit2(m_core.GetVkQueue(queueType), 1, &submitInfo, VK_NULL_HANDLE));
        return signal.current;
    }


    RID Context::CreateComputePipeline(const std::string& name, VkShaderModule shaderModule) {
        ComputePipeline pipeline;
        pipeline.Init(m_core.GetVkDevice(), name, shaderModule, DescriptorSlot::GetGlobalPipelineLayout());
        return m_computePipelines.Add(std::move(pipeline));
    }

    void Context::DestroyComputePipeline(RID rid) {
        ComputePipeline* pipeline = m_computePipelines.Get(rid);
        if (pipeline != nullptr) {
            pipeline->Destroy();
        }
        m_computePipelines.Remove(rid);
    }

    VkPipeline Context::GetComputePipeline(RID rid) {
        ComputePipeline* pipeline = m_computePipelines.Get(rid);
        return pipeline != nullptr ? pipeline->GetVkPipeline() : VK_NULL_HANDLE;
    }

    RID Context::CreateDescriptorSlotBinding(const DescriptorSlotLayout& descriptorSlotLayout) {
        auto descriptorSlotBinding = m_descriptorSlot.CreateSlotBinding(descriptorSlotLayout);
        return m_descriptorSlots.Add(std::move(descriptorSlotBinding));
    }

    void Context::DestroyDescriptorSlotBinding(RID rid) {
        auto* descriptorSlotBinding = m_descriptorSlots.Get(rid);
        if (descriptorSlotBinding) {
            descriptorSlotBinding->Release();
        }
        m_descriptorSlots.Remove(rid);
    }
}
