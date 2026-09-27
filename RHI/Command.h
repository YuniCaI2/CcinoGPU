#pragma once
#include "RHIStruct.h"

#include <vulkan/vulkan_core.h>
#include <unordered_map>
#include <memory>

//TODO这里实现录制任务

namespace Ccino::RHI {
    class CommandContext {
    public:
        CommandContext() = default;
        ~CommandContext() = default;

        void Init(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t frameCount = 1);
        void Destroy();


        VkCommandBuffer GetCommandBuffer(uint32_t queueFamilyIndex, uint32_t frameIndex = 0);
    private:

        struct CommandPoolSlot {
            std::vector<VkCommandPool> commandPools; //10 以内肯定够
            std::vector<std::vector<VkCommandBuffer>> commandBuffers;
        };

        VkCommandPool GetCommandPool(uint32_t queueFamilyIndex);
        CommandPoolSlot& GetThreadSlot();

        std::vector<std::unique_ptr<CommandPoolSlot>> m_threadSlots;

        std::mutex m_mutex;
        VkDevice m_device = VK_NULL_HANDLE;
        uint32_t queueFamilyCount{}; //记录具体的值
        uint32_t m_frameCount = 1;
    };
}
