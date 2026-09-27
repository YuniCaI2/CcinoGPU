#include "RHI/Context.h"
#include <algorithm>
#include <cstdint>
#include <vector>
#include <vulkan/vulkan_core.h>
#include <iostream>
#include <memory.h>

namespace Ccino::RHI {
    void Context::Init(const RHISettings& settings) {
        m_core.Init(settings);
        m_commandContext.Init(m_core.GetVkDevice(), m_core.GetVkPhysicalDevice());
    }

    void Context::Destroy() {
        m_buffers.ForEach([&](VulkanBuffer& buffer) {
            vmaDestroyBuffer(m_core.GetVmaAllocator(), buffer.buffer, buffer.allocation);
        });
        m_commandContext.Destroy();
        m_core.Destroy();
    }

    RID Context::CreateBuffer(const BufferCreateInfo& createInfo) {
        VkBufferCreateInfo bufferCreateInfo{};
        bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferCreateInfo.size = createInfo.size;
        bufferCreateInfo.usage = 0;
        bufferCreateInfo.flags = 0;

        VkBufferUsageFlags2CreateInfo bufferUsageCreateInfo{};
        bufferUsageCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_USAGE_FLAGS_2_CREATE_INFO;
        bufferUsageCreateInfo.usage = createInfo.usage;

        bufferCreateInfo.pNext = &bufferUsageCreateInfo; //这里串联起来


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
        allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
        // 这里错了,上传需要临时创建一个Buffer Staging


        if (createInfo.usage & VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT || createInfo.usage & VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT) {
            allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                VMA_ALLOCATION_CREATE_MAPPED_BIT;
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

        buffer.usage = createInfo.usage;
        buffer.size = createInfo.size;

        return m_buffers.Add(std::move(buffer));
    }

    RID Context::CreateUniformBuffer(uint32_t size, void* data) {
        RID rid = CreateBuffer(BufferCreateInfo{size, VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT});
        if (data == nullptr) {
            MapBuffer(rid);
        } else {
            auto ptr= MapBuffer(rid);
            memcpy(ptr, data, size);
            UnmapBuffer(rid);
        }
        return rid;
    }

    RID Context::CreateTransferBuffer(uint32_t size, void* data) {
        RID rid = CreateBuffer(BufferCreateInfo{size, VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT});
        void* mapped = MapBuffer(rid);

        return rid;
    }

    RID Context::CreateStorageBuffer(uint32_t size, void* data) {
        RID rid = CreateBuffer(BufferCreateInfo{size, VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT}); //传输的目的地
        if (data == nullptr) {
            MapBuffer(rid);
        } else {
            //TODO使用传输队列上传
            //传输是异步的需要使用全局timeline Semphore保证同步
        }
        return rid;
    }

    void* Context::MapBuffer(RID rid) {
        auto* buffer = m_buffers.Get(rid);
        if (buffer->usage & VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT || buffer->usage & VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT) {
            void* mapped = nullptr;
            VULKAN_CHECK(vmaMapMemory(m_core.GetVmaAllocator(), buffer->allocation, &mapped));
            buffer->mappedPtr = mapped;
            return mapped;
        } else {
            std::cerr << "Buffer is not a uniform buffer or transfer source buffer" << std::endl;
            return nullptr;
        }
    }

    void Context::UnmapBuffer(RID rid) {
        auto* buffer = m_buffers.Get(rid);
        if (buffer->usage & VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT || buffer->usage & VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT) {
            vmaUnmapMemory(m_core.GetVmaAllocator(), buffer->allocation);
            buffer->mappedPtr = nullptr;
        } else {
            std::cerr << "Buffer is not a uniform buffer or transfer source buffer" << std::endl;
        }
    }

    void Context::DestroyBuffer(RID rid) {
        VulkanBuffer* buffer = m_buffers.Get(rid);
        if (buffer == nullptr) {
            return;
        }
        vmaDestroyBuffer(m_core.GetVmaAllocator(), buffer->buffer, buffer->allocation);
        m_buffers.Remove(rid);
    }

}