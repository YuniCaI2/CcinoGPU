#pragma once
#include "RHIStruct.h"

#include <vulkan/vulkan_core.h>
#include <unordered_map>
#include <memory>

//TODO这里实现录制任务

namespace Ccino::RHI {
    class CommandContext {
    public:
        struct CommandBuffer {
            VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
            VkCommandPool commandPool = VK_NULL_HANDLE;
            uint32_t queueFamilyIndex = 0;
            uint32_t frameIndex = 0;

            //线程信息，不知道是否需要记录
        };

        CommandContext() = default;
        ~CommandContext() = default;

        void Init(VkDevice device, const std::vector<uint32_t>& queueFamilyIndices, uint32_t frameCount = 1);
        void Destroy();


        CommandBuffer& GetCommandBuffer(uint32_t queueFamilyIndex, uint32_t frameIndex = 0);

    private:

        struct CommandPoolSlot {
            std::vector<VkCommandPool> commandPools; //10 以内肯定够
            std::vector<std::vector<CommandBuffer>> commandBuffers;
        };

        VkCommandPool GetCommandPool(uint32_t queueFamilyIndex);
        CommandPoolSlot& GetThreadSlot();

        std::vector<std::unique_ptr<CommandPoolSlot>> m_threadSlots;

        std::mutex m_mutex;
        VkDevice m_device = VK_NULL_HANDLE;
        std::vector<uint32_t> m_queueFamilyIndices; //只能是创建设备时申请了队列的家族
        uint32_t queueFamilyCount{}; //最大家族索引 + 1，commandPools 按家族索引寻址
        uint32_t m_frameCount = 1;
    };
}
