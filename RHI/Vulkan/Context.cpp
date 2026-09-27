#include "RHI/Vulkan/Context.h"
#include <vulkan/vulkan_core.h>
#include <stdexcept>



namespace  {

}

namespace Ccino::RHI::Vulkan {
    void Context::Init(const RHISettings& settings) {
        m_core.Init(settings);
    }

    void Context::Destroy() {
        m_core.Destroy();
    }

    RID Context::CreateBuffer(const BufferCreateInfo& createInfo) {
        VkBufferCreateInfo bufferCreateInfo{};
        bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferCreateInfo.size = createInfo.size;
        bufferCreateInfo.usage = static_cast<VkBufferUsageFlags>(createInfo.usage);
        bufferCreateInfo.sharingMode = VK_SHARING_MODE_CONCURRENT; //这里使用共享模型
        bufferCreateInfo.flags = 0;

        VkBuffer buffer;
        VULKAN_CHECK(vkCreateBuffer(m_core.GetVkDevice(), &bufferCreateInfo, nullptr, &buffer));
    }

    void Context::DestroyBuffer(RID rid) {

    }

}