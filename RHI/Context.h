#pragma once
#include "RHI/RHIStruct.h"
#include "RHI/Core.h"
#include "RHI/Command.h"
#include <vulkan/vulkan_core.h>


namespace Ccino::RHI {

    struct BufferCreateInfo {
        uint32_t size = 0;
        VkBufferUsageFlags2 usage = 0;
        //...
    };

    class Context {
    public:
        Context();
        ~Context() = default;

        void Init(const RHISettings& settings);
        void Destroy();

        //Buffer 默认使用BDA
        RID CreateBuffer(const BufferCreateInfo& createInfo); //这里使用新的的BufferUsageFlags2
        RID CreateUniformBuffer(uint32_t size, void* data); //Uniform Buffer 
        RID CreateStorageBuffer(uint32_t size, void* data);
        RID CreateTransferBuffer(uint32_t size, void* data); //StagingBuffer用来上传
        
        void* MapBuffer(RID rid); //必须使用可以支持CPU映射的Buffer
        void UnmapBuffer(RID rid);
        void DestroyBuffer(RID rid);

        //Texture


    private:
        struct VulkanBuffer {
            VkBuffer buffer = VK_NULL_HANDLE;
            VmaAllocation allocation = VK_NULL_HANDLE;

            //MetaInfo
            uint32_t size = 0;
            VkBufferUsageFlags2 usage = 0;

            void* mappedPtr = nullptr;
            uint64_t bda = 0; //GPU的地址
        };

        Core m_core;
        CommandContext m_commandContext;
        TRIDArray<VulkanBuffer> m_buffers;
    };
}
