#include "RHI/DescriptorSlot.h"
#include "RHI/Context.h"
#include "RHI/Core.h"
#include <vulkan/vulkan_core.h>
#include <cstring>
#include <stdexcept>

namespace Ccino::RHI {
    namespace {
        bool BufferUsageMatchesLayout(VkDescriptorType type, VkBufferUsageFlags2 usage) {
            switch (type) {
                case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                    return (usage & VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT) != 0;
                case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                    return (usage & VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT) != 0;
                case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
                    return (usage & VK_BUFFER_USAGE_2_UNIFORM_TEXEL_BUFFER_BIT) != 0;
                case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
                    return (usage & VK_BUFFER_USAGE_2_STORAGE_TEXEL_BUFFER_BIT) != 0;
                default:
                    return false;
            }
        }
        VkDescriptorType GetDescriptorType(VkBufferUsageFlags2 usage) {
            switch (usage) {
                case VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT:
                    return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                case VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT:
                    return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                default:
                    throw std::runtime_error("Invalid buffer usage");
            }
        }
    }

    void DescriptorSlot::Init(Context* context) {
        m_context = context;
        m_core = &context->GetCore();
        m_device = m_core->GetVkDevice();
        m_physicalDevice = m_core->GetVkPhysicalDevice();


        //获得对齐
        VkPhysicalDeviceDescriptorHeapPropertiesEXT descriptorHeapProperties;
        descriptorHeapProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT;
        descriptorHeapProperties.pNext = nullptr;

        VkPhysicalDeviceProperties2 properties2;
        properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties2.pNext = &descriptorHeapProperties;
        vkGetPhysicalDeviceProperties2(m_physicalDevice, &properties2); //查询必要的信息

        m_samplerDescriptorAlignment = descriptorHeapProperties.samplerDescriptorAlignment;
        m_bufferDescriptorAlignment = descriptorHeapProperties.bufferDescriptorAlignment;
        m_imageDescriptorAlignment = descriptorHeapProperties.imageDescriptorAlignment;
        m_samplerDescriptorSize = descriptorHeapProperties.samplerDescriptorSize;
        m_bufferDescriptorSize = descriptorHeapProperties.bufferDescriptorSize;
        m_imageDescriptorSize = descriptorHeapProperties.imageDescriptorSize;
        m_reversedDescriptorSize = descriptorHeapProperties.minResourceHeapReservedRange;

        m_samplerDescriptorStride = ALIGN_UP(m_samplerDescriptorSize, m_samplerDescriptorAlignment);
        m_bufferDescriptorStride = ALIGN_UP(m_bufferDescriptorSize, m_bufferDescriptorAlignment);
        m_imageDescriptorStride = ALIGN_UP(m_imageDescriptorSize, m_imageDescriptorAlignment);

        //初始化PushConstant的内存
        m_pushConstantData = new char[128]; 

        CreateAllSampler();
    }

    void DescriptorSlot::Destroy() {

        DestroyAllSampler();
        m_context = nullptr;
        m_core = nullptr;
        m_device = VK_NULL_HANDLE;
        m_physicalDevice = VK_NULL_HANDLE;
        delete [] m_pushConstantData; //析构PushConstant Data槽位

    }

    bool DescriptorSlot::IsIndirectPushConstant(const DescriptorSlotLayout& layout) const {
        //计算下当前的PushConstant的大小
        size_t size = 0;
        for (const auto& [name, descriptor] : layout) {
            if (descriptor.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
                size += sizeof(uint64_t);
            } else if (descriptor.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
                size += sizeof(uint32_t);
            } else if (descriptor.type == VK_DESCRIPTOR_TYPE_SAMPLER) {
                size += sizeof(uint32_t);
            }
        }
        return size > 128;
    }


    DescriptorSlot& DescriptorSlot::AddBuffer(const std::string& name, RID rid) {
        const Descriptor* layoutDescriptor = nullptr;
        for (const auto& [n, descriptor] : m_descriptorSlotLayout) {
            if (n == name) {
                layoutDescriptor = &descriptor;
                break;
            }
        }

        if (layoutDescriptor == nullptr) {
            throw std::runtime_error("Buffer not found in descriptor slot layout");
        }

        VkBufferUsageFlags2 usage = 0;
        if (m_context == nullptr || !m_context->GetBufferUsage(rid, usage)) {
            throw std::runtime_error("Buffer resource does not exist");
        }
        if (!BufferUsageMatchesLayout(layoutDescriptor->type, usage)) {
            throw std::runtime_error("Buffer type does not match descriptor slot layout");
        }

        uint32_t index = 0;
        if (m_bufferOffsetMap.find(rid) == m_bufferOffsetMap.end()) {
            if (m_bufferReleaseQueue.empty()) {
                m_bufferRIDList.push_back(rid);
                index = m_bufferRIDList.size() - 1;
                m_bufferOffsetMap[rid] = index;
            } else {
                uint32_t freeSlot = m_bufferReleaseQueue.front();
                m_bufferReleaseQueue.pop();
                m_bufferRIDList[freeSlot] = rid;
                index = freeSlot;
                m_bufferOffsetMap[rid] = index;
            }
        } else {
            index = m_bufferOffsetMap[rid];
        }
        uint64_t offset = index * m_bufferDescriptorStride;
        std::memcpy(m_pushConstantData + m_pushConstantOffsetMap[name], &offset, sizeof(uint64_t));

        VkHostAddressRangeEXT dst{};
        dst.address = (static_cast<char*>(m_context->GetBufferMappedPtr(m_resourceHeapBuffer)) + offset);
        dst.size = m_bufferDescriptorSize;

        VkDeviceAddressRangeEXT addressRange{};
        addressRange.address = m_context->GetBufferBDA(rid);
        addressRange.size = m_context->GetBufferSize(rid);

        VkResourceDescriptorInfoEXT resourceDescriptorInfo{};
        resourceDescriptorInfo.sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT;
        resourceDescriptorInfo.type = GetDescriptorType(usage);
        resourceDescriptorInfo.data.pAddressRange = &addressRange;

        VULKAN_CHECK(m_context->GetCore().GetVkWriteResourceDescriptorsEXT()(m_device, 1, &resourceDescriptorInfo, &dst));

        return *this;
    }

    DescriptorSlot& DescriptorSlot::AddTexture(const std::string& name, RID rid, VkImageSubresourceRange subresourceRange) {
        const Descriptor* layoutDescriptor = nullptr;
        for (const auto& [n, descriptor] : m_descriptorSlotLayout) {
            if (n == name) {
                layoutDescriptor = &descriptor;
                break;
            }
        }

        if (layoutDescriptor == nullptr) {
            throw std::runtime_error("Texture not found in descriptor slot layout");
        }

        VkDescriptorType resourceType = VK_DESCRIPTOR_TYPE_MAX_ENUM;
        if (m_context == nullptr || !m_context->GetImageDescriptorType(rid, resourceType)) {
            throw std::runtime_error("Texture resource does not exist");
        }
        if (resourceType != layoutDescriptor->type) {
            throw std::runtime_error("Texture type does not match descriptor slot layout");
        }

        uint32_t index = 0;
        if (m_resourceOffsetMap.find(rid) == m_resourceOffsetMap.end()) {
            if (m_resourceReleaseQueue.empty()) {
                m_resourceRIDList.push_back(rid);
                index = m_resourceRIDList.size() - 1;
                m_resourceOffsetMap[rid] = index;
            } else {
                uint32_t freeSlot = m_resourceReleaseQueue.front();
                m_resourceReleaseQueue.pop();
                m_resourceRIDList[freeSlot] = rid;
                index = freeSlot;
                m_resourceOffsetMap[rid] = index;
            }
            //计算Descriptor Heap中的偏移量
        } else {
            index = m_resourceOffsetMap[rid];
        }
        uint32_t offset = index * m_imageDescriptorStride;
        std::memcpy(m_pushConstantData + m_pushConstantOffsetMap[name], &offset, sizeof(uint32_t));

        //TODO: 绑定图像。 等待RHI支持Texture的创建后实现

        return *this;
    }

    DescriptorSlot& DescriptorSlot::AddSampler(const std::string& name, SamplerType type) {
        uint32_t offset = static_cast<uint32_t>(type) * m_samplerDescriptorStride;
        std::memcpy(m_pushConstantData + m_pushConstantOffsetMap[name], &offset, sizeof(uint32_t));

        return *this;
    }


    DescriptorSlot& DescriptorSlot::DeleteBuffer(RID rid) {
        auto it = m_bufferOffsetMap.find(rid);
        if (it == m_bufferOffsetMap.end()) {
            return *this;
        }
        uint32_t index = it->second;
        m_bufferRIDList[index] = RID::Invalid();
        m_bufferReleaseQueue.push(index);
        m_bufferOffsetMap.erase(it);
        return *this;
    }

    DescriptorSlot& DescriptorSlot::DeleteTexture(RID rid) {
        auto it = m_resourceOffsetMap.find(rid);
        if (it == m_resourceOffsetMap.end()) {
            return *this;
        }
        uint32_t index = it->second;
        m_resourceRIDList[index] = RID::Invalid();
        m_resourceReleaseQueue.push(index);
        m_resourceOffsetMap.erase(it);
        return *this;
    }

    void DescriptorSlot::GeneratePushConstantPackageBuffer(Context& context) {
        if (IsIndirectPushConstant(m_descriptorSlotLayout)) {
            BufferCreateInfo createInfo{};
            createInfo.hostVisible = true;
            createInfo.size = sizeof(PushConstantPackageIndirect);
            createInfo.usage = VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT; //使用Storage Buffer 来存储间接索引
            m_pushConstantPackageRID = context.CreateBuffer(createInfo);
        }
    }

    RID DescriptorSlot::GenerateDescriptorHeapBuffer(Context& context, uint32_t size) {
        BufferCreateInfo createInfo{};
        createInfo.hostVisible = true;
        createInfo.size = size;
        createInfo.usage = VK_BUFFER_USAGE_2_DESCRIPTOR_HEAP_BIT_EXT;
        return context.CreateBuffer(createInfo);
    }
    
    
    void DescriptorSlot::BindLayout(const DescriptorSlotLayout& layout) {
        m_descriptorSlotLayout = layout; //绑定布局
        //TODO: Indirect Push Constant 
        //为支持Indirect Push Constant 需要为每个不同的Material 都创建一个PushConstant Package Buffer 或者退化到UniformBuffer 实现过渡，这里先不支持
        if (IsIndirectPushConstant(m_descriptorSlotLayout)) {
            throw std::runtime_error("Indirect Push Constant is not supported yet");
        }
        uint32_t offset = 0;
        //为每个Push Constant 分配偏移量
        for (const auto& [name, descriptor] : m_descriptorSlotLayout) {
            m_pushConstantOffsetMap[name] = offset;

            if (descriptor.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
                offset += sizeof(uint32_t);
            } else if (descriptor.type == VK_DESCRIPTOR_TYPE_SAMPLER) {
                offset += sizeof(uint32_t);
            }  else if (descriptor.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
                offset += sizeof(uint64_t);
            } else if (descriptor.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
                offset += sizeof(uint64_t);
            } else if (descriptor.type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
                offset += sizeof(uint32_t);
            } else if (descriptor.type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER) {
                offset += sizeof(uint64_t);
            }
            //自然是对齐的不用处理
        }
    }


    void DescriptorSlot::CreateAllSampler() { //创建所有枚举变量的enum
        //直接塞
        // 1. 先创建DescriptorHeap
        // 计算Sampler大小
        uint32_t samplerSize = m_samplerDescriptorStride * static_cast<uint32_t>(SamplerType::COUNT);
        m_samplerHeapBuffer = GenerateDescriptorHeapBuffer(*m_context, samplerSize + m_reversedDescriptorSize); //这里需要增加保留范围， 给驱动使用

        // 2. 创建Sampler并且绑定
        for (uint32_t i = 0; i < static_cast<uint32_t>(SamplerType::COUNT); i++) {
            SamplerType samplerType = static_cast<SamplerType>(i);
            VkSamplerCreateInfo createInfo = CreateSamplerCreateInfo(samplerType);
            VkHostAddressRangeEXT dst{};
            auto* mapped = static_cast<char*>(m_context->GetBufferMappedPtr(m_samplerHeapBuffer));
            dst.address = mapped + i * m_samplerDescriptorStride;
            dst.size = m_samplerDescriptorSize;
            VULKAN_CHECK(m_context->GetCore().GetVkWriteSamplerDescriptorsEXT()(m_device, 1,&createInfo, &dst));
        }
    }

    void DescriptorSlot::DestroyAllSampler() { 
        m_context->DestroyBuffer(m_samplerHeapBuffer);
        m_samplerHeapBuffer = RID::Invalid();
    }
}