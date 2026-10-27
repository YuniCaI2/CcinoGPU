#include "Command.h"
#include "Context.h"
#include "Core.h"
#include <algorithm>
#include <stdexcept>

namespace Ccino::RHI {
    void CommandContext::Init(VkDevice device, const std::vector<uint32_t>& queueFamilyIndices, uint32_t frameCount) {
        m_device = device;
        m_frameCount = frameCount;
        m_queueFamilyIndices.clear();
        queueFamilyCount = 0;
        for (uint32_t family : queueFamilyIndices) {
            if (std::find(m_queueFamilyIndices.begin(), m_queueFamilyIndices.end(), family) == m_queueFamilyIndices.end()) {
                m_queueFamilyIndices.push_back(family);
                queueFamilyCount = std::max(queueFamilyCount, family + 1);
            }
        }
    }

    void CommandContext::Destroy() {
        for (auto& [_, threadSlot] : m_threadSlots) {
            for (VkCommandPool pool : threadSlot->commandPools) {
                if (pool != VK_NULL_HANDLE) {
                    vkDestroyCommandPool(m_device, pool, nullptr);
                }
            }
        }
        m_threadSlots.clear();
        m_device = VK_NULL_HANDLE;
    }
    CommandContext::CommandBuffer& CommandContext::GetCommandBuffer(uint32_t queueFamilyIndex, uint32_t frameIndex) {
        CommandPoolSlot& slot = GetThreadSlot();
        if (queueFamilyIndex >= slot.commandBuffers.size() || slot.commandBuffers[queueFamilyIndex].empty()) {
            throw std::runtime_error("Queue family has no command pool");
        }
        return slot.commandBuffers[queueFamilyIndex][frameIndex];
    }


    VkCommandPool CommandContext::GetCommandPool(uint32_t queueFamilyIndex) {
        return GetThreadSlot().commandPools[queueFamilyIndex];
    }

    CommandContext::CommandPoolSlot& CommandContext::GetThreadSlot() {
        const std::thread::id threadId = std::this_thread::get_id();
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_threadSlots.find(threadId);
        if (it == m_threadSlots.end()) {
            auto newSlot = std::make_unique<CommandPoolSlot>();
            CommandPoolSlot* slot = newSlot.get();
            slot->commandPools.resize(queueFamilyCount);
            slot->commandBuffers.resize(queueFamilyCount);
            //创建CommandPool，允许单独 reset 其中的 CommandBuffer
            for (uint32_t queueFamilyIndex : m_queueFamilyIndices) {
                VkCommandPoolCreateInfo createInfo{};
                createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
                createInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
                createInfo.queueFamilyIndex = queueFamilyIndex;
                VULKAN_CHECK(vkCreateCommandPool(m_device, &createInfo, nullptr, &slot->commandPools[queueFamilyIndex]));

                slot->commandBuffers[queueFamilyIndex].resize(m_frameCount);
                std::vector<VkCommandBuffer> allocated(m_frameCount);
                VkCommandBufferAllocateInfo allocateInfo{};
                allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                allocateInfo.commandPool = slot->commandPools[queueFamilyIndex];
                allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                allocateInfo.commandBufferCount = m_frameCount;
                VULKAN_CHECK(vkAllocateCommandBuffers(m_device, &allocateInfo, allocated.data()));
                for (uint32_t frameIndex = 0; frameIndex < m_frameCount; ++frameIndex) {
                    CommandBuffer& commandBuffer = slot->commandBuffers[queueFamilyIndex][frameIndex];
                    commandBuffer.commandBuffer = allocated[frameIndex];
                    commandBuffer.commandPool = slot->commandPools[queueFamilyIndex];
                    commandBuffer.queueFamilyIndex = queueFamilyIndex;
                    commandBuffer.frameIndex = frameIndex;
                }
            }
            it = m_threadSlots.emplace(threadId, std::move(newSlot)).first;
        }
        return *it->second;
    }
}