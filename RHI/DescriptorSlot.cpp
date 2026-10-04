#include "RHI/DescriptorSlot.h"
#include "RHI/Context.h"
#include "RHI/Core.h"
#include <mutex>
#include <vulkan/vulkan_core.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace Ccino::RHI {
    namespace {
        bool IsSupportedDescriptorType(VkDescriptorType type) {
            switch (type) {
                case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
                case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                case VK_DESCRIPTOR_TYPE_SAMPLER:
                    return true;
                default:
                    return false;
            }
        }

        bool BufferUsageMatchesLayout(VkDescriptorType type, VkBufferUsageFlags2 usage) {
            switch (type) {
                case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                    return (usage & VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT) != 0;
                case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                    return (usage & VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT) != 0;
                default:
                    return false;
            }
        }

        uint32_t AcquireHeapIndex(std::vector<std::pair<RID, uint32_t>>& ridList, std::queue<uint32_t>& releaseQueue, uint32_t maxCount, RID rid) {
            if (!releaseQueue.empty()) {
                uint32_t index = releaseQueue.front();
                releaseQueue.pop();
                ridList[index] = {rid, 1};
                return index;
            }
            if (ridList.size() >= maxCount) {
                throw std::runtime_error("Descriptor heap is full");
            }
            ridList.emplace_back(rid, 1);
            return static_cast<uint32_t>(ridList.size() - 1);
        }
    }

    DescriptorSlotBinding::DescriptorSlotBinding(DescriptorSlot* slotContext, DescriptorSlotLayout layout)
        : m_slotContext(slotContext), m_layout(std::move(layout)) {}

    DescriptorSlotBinding::DescriptorSlotBinding(DescriptorSlotBinding&& other) noexcept
        : m_slotContext(std::exchange(other.m_slotContext, nullptr)),
          m_layout(std::move(other.m_layout)),
          m_fields(other.m_fields),
          m_block(std::exchange(other.m_block, INVALID_BLOCK)),
          m_dirty(other.m_dirty) {}

    DescriptorSlotBinding& DescriptorSlotBinding::operator=(DescriptorSlotBinding&& other) noexcept {
        if (this != &other) {
            Release();
            m_slotContext = std::exchange(other.m_slotContext, nullptr);
            m_layout = std::move(other.m_layout);
            m_fields = other.m_fields;
            m_block = std::exchange(other.m_block, INVALID_BLOCK);
            m_dirty = other.m_dirty;
        }
        return *this;
    }

    DescriptorSlotBinding::~DescriptorSlotBinding() {
        Release();
    }

    void DescriptorSlotBinding::Release() {
        if (m_slotContext == nullptr) {
            return;
        }
        if (m_block != INVALID_BLOCK) {
            m_slotContext->FreeSlotBlock(m_block);
            m_block = INVALID_BLOCK;
        }
        //释放槽位
        for (const auto& [name, entry] : m_fieldMap) {
            if (entry.type == ResourceType::Buffer) {
                m_slotContext->ReleaseBuffer(entry.rid);
            } else if (entry.type == ResourceType::Texture) {
                m_slotContext->ReleaseTexture(entry.rid);
            } 
        }

        m_slotContext->m_liveBindingCount.fetch_sub(1, std::memory_order_relaxed);
        m_slotContext = nullptr;
    }

    uint32_t DescriptorSlotBinding::FindField(const std::string& name, VkDescriptorType& type) const {
        for (size_t i = 0; i < m_layout.size(); i++) {
            if (m_layout[i].first == name) {
                type = m_layout[i].second.type;
                return static_cast<uint32_t>(i);
            }
        }
        throw std::runtime_error("Descriptor not found in descriptor slot layout: " + name);
    }

    DescriptorSlotBinding& DescriptorSlotBinding::BindTexture(const std::string& name, RID rid, VkImageSubresourceRange subresourceRange) {
        VkDescriptorType type = VK_DESCRIPTOR_TYPE_MAX_ENUM;
        const uint32_t field = FindField(name, type);
        if (m_fieldMap.find(name) != m_fieldMap.end()) {
            m_slotContext->ReleaseTexture(m_fieldMap[name].rid);
        } 
        m_fieldMap[name] = ResourceEntry{rid, ResourceType::Texture};
        m_fields[field] = m_slotContext->AcquireTextureIndex(rid, type, subresourceRange);
        m_dirty = true;
        return *this;
    }

    DescriptorSlotBinding& DescriptorSlotBinding::BindBuffer(const std::string& name, RID rid) {
        VkDescriptorType type = VK_DESCRIPTOR_TYPE_MAX_ENUM;
        const uint32_t field = FindField(name, type);
        if (m_fieldMap.find(name) != m_fieldMap.end()) {
            m_slotContext->ReleaseBuffer(m_fieldMap[name].rid);
        }
        m_fieldMap[name] = ResourceEntry{rid, ResourceType::Buffer}; 
        m_fields[field] = m_slotContext->AcquireBufferIndex(rid, type); 
        m_dirty = true;
        return *this;
    }

    DescriptorSlotBinding& DescriptorSlotBinding::BindSampler(const std::string& name, SamplerType type) {
        VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_MAX_ENUM;
        const uint32_t field = FindField(name, descriptorType);
        if (descriptorType != VK_DESCRIPTOR_TYPE_SAMPLER) {
            throw std::runtime_error("Sampler type does not match descriptor slot layout");
        }
        m_fields[field] = m_slotContext->GetSamplerIndex(type);
        m_dirty = true;
        return *this;
    }

    void DescriptorSlotBinding::Push(VkCommandBuffer commandBuffer) {
        if (m_dirty) {
            const uint32_t block = m_slotContext->AllocateSlotBlock();
            std::memcpy(m_slotContext->GetSlotBlockMapped(block), m_fields.data(), m_layout.size() * sizeof(uint32_t));
            if (m_block != INVALID_BLOCK) {
                m_slotContext->FreeSlotBlock(m_block);
            }
            m_block = block;
            m_dirty = false;
        }

        const VkDeviceAddress address = m_slotContext->GetSlotBlockAddress(m_block);
        VkPushDataInfoEXT pushDataInfo{};
        pushDataInfo.sType = VK_STRUCTURE_TYPE_PUSH_DATA_INFO_EXT;
        pushDataInfo.offset = 0;
        pushDataInfo.data.address = &address;
        pushDataInfo.data.size = sizeof(address);
        m_slotContext->m_core->GetVkCmdPushDataEXT()(commandBuffer, &pushDataInfo);
    }

    void DescriptorSlot::Init(Context* context) {
        m_context = context;
        m_core = &context->GetCore();
        m_device = m_core->GetVkDevice();
        m_physicalDevice = m_core->GetVkPhysicalDevice();


        //获得对齐
        VkPhysicalDeviceDescriptorHeapPropertiesEXT descriptorHeapProperties{};
        descriptorHeapProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT;
        descriptorHeapProperties.pNext = nullptr;

        VkPhysicalDeviceProperties2 properties2{};
        properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties2.pNext = &descriptorHeapProperties;
        vkGetPhysicalDeviceProperties2(m_physicalDevice, &properties2); //查询必要的信息

        m_samplerDescriptorSize = descriptorHeapProperties.samplerDescriptorSize;
        m_samplerDescriptorAlignment = descriptorHeapProperties.samplerDescriptorAlignment;
        m_bufferDescriptorSize = descriptorHeapProperties.bufferDescriptorSize;
        m_bufferDescriptorAlignment = descriptorHeapProperties.bufferDescriptorAlignment;
        m_imageDescriptorSize = descriptorHeapProperties.imageDescriptorSize;
        m_imageDescriptorAlignment = descriptorHeapProperties.imageDescriptorAlignment;
        m_resourceHeapAlignment = descriptorHeapProperties.resourceHeapAlignment;
        m_samplerHeapAlignment = descriptorHeapProperties.samplerHeapAlignment;
        m_resourceHeapReservedSize = descriptorHeapProperties.minResourceHeapReservedRange;
        m_samplerHeapReservedSize = descriptorHeapProperties.minSamplerHeapReservedRange;

        CreateAllSampler();
        m_resourceHeapBuffer = GenerateResourceHeapBuffer(*context);
        m_slotBuffer = GenerateSlotBuffer(*context);

        const uint32_t flightFrameCount = context->GetFlightFrameCount();
        m_currentFrame = 0;
        m_slotBlockPendingRelease.assign(flightFrameCount, {});
        m_bufferPendingRelease.assign(flightFrameCount, {});
        m_texturePendingRelease.assign(flightFrameCount, {});
    }

    void DescriptorSlot::Update() {
        std::scoped_lock lock(m_slotMutex, m_heapMutex);
        m_currentFrame = (m_currentFrame + 1) % m_context->GetFlightFrameCount();

        for (uint32_t block : m_slotBlockPendingRelease[m_currentFrame]) {
            m_freeSlotBlocks.push_back(block);
        }
        m_slotBlockPendingRelease[m_currentFrame].clear();
        for (uint32_t index : m_bufferPendingRelease[m_currentFrame]) {
            m_bufferReleaseQueue.push(index);
        }
        m_bufferPendingRelease[m_currentFrame].clear();
        for (uint32_t index : m_texturePendingRelease[m_currentFrame]) {
            m_textureReleaseQueue.push(index);
        }
        m_texturePendingRelease[m_currentFrame].clear();
    }

    void DescriptorSlot::Destroy() {
        if (m_liveBindingCount.load(std::memory_order_relaxed) != 0) {
            throw std::runtime_error("DescriptorSlot destroyed while bindings are still alive");
        }

        DestroyAllSampler();
        m_context->DestroyBuffer(m_resourceHeapBuffer);
        m_context->DestroyBuffer(m_slotBuffer);
        m_resourceHeapBuffer = RID::Invalid();
        m_resourceHeapMapped = nullptr;
        m_resourceHeapBindInfo = {};
        m_slotBuffer = RID::Invalid();
        m_slotBufferMapped = nullptr;
        m_slotBufferAddress = 0;

        m_slotBlockCount = 0;
        m_freeSlotBlocks.clear();
        m_slotBlockPendingRelease.clear();
        m_textureRIDList.clear();
        m_bufferRIDList.clear();
        m_textureIndexMap.clear();
        m_bufferIndexMap.clear();
        m_textureReleaseQueue = {};
        m_bufferReleaseQueue = {};
        m_texturePendingRelease.clear();
        m_bufferPendingRelease.clear();

        m_context = nullptr;
        m_core = nullptr;
        m_device = VK_NULL_HANDLE;
        m_physicalDevice = VK_NULL_HANDLE;
    }

    DescriptorSlotBinding DescriptorSlot::CreateSlotBinding(DescriptorSlotLayout layout) {
        if (layout.size() > DescriptorSlotBinding::MAX_FIELD_COUNT) {
            throw std::runtime_error("Descriptor slot layout has too many fields");
        }
        for (const auto& [name, descriptor] : layout) {
            if (!IsSupportedDescriptorType(descriptor.type)) {
                throw std::runtime_error("Descriptor type is not supported by descriptor slot: " + name);
            }
        }

        m_liveBindingCount.fetch_add(1, std::memory_order_relaxed);
        return DescriptorSlotBinding(this, std::move(layout));
    }

    void DescriptorSlot::BindHeaps(VkCommandBuffer commandBuffer) const {
        m_core->GetVkCmdBindResourceHeapEXT()(commandBuffer, &m_resourceHeapBindInfo);
        m_core->GetVkCmdBindSamplerHeapEXT()(commandBuffer, &m_samplerHeapBindInfo);
    }

    void DescriptorSlot::ReleaseBuffer(RID rid) {
        std::lock_guard lock(m_heapMutex);
        auto it = m_bufferIndexMap.find(rid);
        if (it == m_bufferIndexMap.end()) {
            return;
        }
        uint32_t index = it->second.index;
        if (m_bufferRIDList[index].second > 0) {
            m_bufferRIDList[index].second--;
        } 
        if (m_bufferRIDList[index].second <= 0) {
            m_bufferRIDList[index] = std::pair<RID, uint32_t>(RID::Invalid(), 0);
            m_bufferPendingRelease[m_currentFrame].push_back(index);
            m_bufferIndexMap.erase(it);
        }
    }

    void DescriptorSlot::ReleaseTexture(RID rid) {
        std::lock_guard lock(m_heapMutex);
        auto it = m_textureIndexMap.find(rid);
        if (it == m_textureIndexMap.end()) {
            return;
        }
        uint32_t index = it->second.index;
        if (m_textureRIDList[index].second > 0) {
            m_textureRIDList[index].second--;
        } 
        if (m_textureRIDList[index].second <= 0) {
            m_textureRIDList[index] = std::pair<RID, uint32_t>(RID::Invalid(), 0);
            m_texturePendingRelease[m_currentFrame].push_back(index);
            m_textureIndexMap.erase(it);
        }
    }

    uint32_t DescriptorSlot::AcquireBufferIndex(RID rid, VkDescriptorType type) {
        VkBufferUsageFlags2 usage = 0;
        if (!m_context->GetBufferUsage(rid, usage)) {
            throw std::runtime_error("Buffer resource does not exist");
        }
        if (!BufferUsageMatchesLayout(type, usage)) {
            throw std::runtime_error("Buffer type does not match descriptor slot layout");
        }

        const uint32_t firstIndex = static_cast<uint32_t>(m_bufferOffset / m_bufferDescriptorSize);

        std::lock_guard lock(m_heapMutex);
        if (auto it = m_bufferIndexMap.find(rid); it != m_bufferIndexMap.end()) {
            if (it->second.type != type) {
                throw std::runtime_error("Buffer is already in the descriptor heap with another descriptor type");
            }
            m_bufferRIDList[it->second.index].second++; //增加计数
            return firstIndex + it->second.index;
        }

        const uint32_t index = AcquireHeapIndex(m_bufferRIDList, m_bufferReleaseQueue, DESCRIPTOR_MAX_BUFFER_COUNT, rid);

        VkDeviceAddressRangeEXT addressRange{};
        addressRange.address = m_context->GetBufferBDA(rid);
        addressRange.size = m_context->GetBufferSize(rid);

        VkResourceDescriptorInfoEXT resourceDescriptorInfo{};
        resourceDescriptorInfo.sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT;
        resourceDescriptorInfo.type = type;
        resourceDescriptorInfo.data.pAddressRange = &addressRange;

        VkHostAddressRangeEXT dst{};
        dst.address = m_resourceHeapMapped + m_bufferOffset + index * m_bufferDescriptorSize;
        dst.size = m_bufferDescriptorSize;

        VULKAN_CHECK(m_core->GetVkWriteResourceDescriptorsEXT()(m_device, 1, &resourceDescriptorInfo, &dst));

        m_bufferIndexMap[rid] = HeapEntry{index, type};
        return firstIndex + index;
    }

    uint32_t DescriptorSlot::AcquireTextureIndex(RID rid, VkDescriptorType type, [[maybe_unused]] const VkImageSubresourceRange& subresourceRange) {
        VkDescriptorType resourceType = VK_DESCRIPTOR_TYPE_MAX_ENUM;
        if (!m_context->GetImageDescriptorType(rid, resourceType)) {
            throw std::runtime_error("Texture resource does not exist");
        }
        if (resourceType != type) {
            throw std::runtime_error("Texture type does not match descriptor slot layout");
        }

        const uint32_t firstIndex = static_cast<uint32_t>(m_textureOffset / m_imageDescriptorSize);

        std::lock_guard lock(m_heapMutex);
        if (auto it = m_textureIndexMap.find(rid); it != m_textureIndexMap.end()) {
            if (it->second.type != type) {
                throw std::runtime_error("Texture is already in the descriptor heap with another descriptor type");
            }
            m_textureRIDList[it->second.index].second++; //增加计数
            return firstIndex + it->second.index;
        }

        const uint32_t index = AcquireHeapIndex(m_textureRIDList, m_textureReleaseQueue, DESCRIPTOR_MAX_IMAGE_COUNT, rid);

        //TODO: 绑定图像。 等待RHI支持Texture的创建后实现, 不同的subresourceRange 需要各自的Descriptor

        m_textureIndexMap[rid] = HeapEntry{index, type};
        return firstIndex + index;
    }

    uint32_t DescriptorSlot::GetSamplerIndex(SamplerType type) const {
        if (type >= SamplerType::COUNT) {
            throw std::runtime_error("Invalid sampler type");
        }
        return static_cast<uint32_t>(type);
    }

    uint32_t DescriptorSlot::AllocateSlotBlock() {
        std::lock_guard lock(m_slotMutex);
        if (!m_freeSlotBlocks.empty()) {
            const uint32_t block = m_freeSlotBlocks.back();
            m_freeSlotBlocks.pop_back();
            return block;
        }
        if (m_slotBlockCount >= DESCRIPTOR_MAX_SLOT_COUNT) {
            throw std::runtime_error("Descriptor slot buffer is full");
        }
        return m_slotBlockCount++;
    }

    void DescriptorSlot::FreeSlotBlock(uint32_t block) {
        std::lock_guard lock(m_slotMutex);
        m_slotBlockPendingRelease[m_currentFrame].push_back(block);
    }

    char* DescriptorSlot::GetSlotBlockMapped(uint32_t block) const {
        return m_slotBufferMapped + static_cast<size_t>(block) * DESCRIPTOR_SLOT_BLOCK_SIZE;
    }

    VkDeviceAddress DescriptorSlot::GetSlotBlockAddress(uint32_t block) const {
        return m_slotBufferAddress + static_cast<VkDeviceAddress>(block) * DESCRIPTOR_SLOT_BLOCK_SIZE;
    }


    RID DescriptorSlot::GenerateDescriptorHeapBuffer(Context& context, VkDeviceSize heapAlignment, VkBindHeapInfoEXT& bindInfo, char*& mapped) {
        BufferCreateInfo createInfo{};
        createInfo.hostVisible = true;
        createInfo.size = static_cast<uint32_t>(bindInfo.heapRange.size + heapAlignment); //多留出对齐堆起点的余量
        createInfo.usage = VK_BUFFER_USAGE_2_DESCRIPTOR_HEAP_BIT_EXT;
        RID rid = context.CreateBuffer(createInfo);

        const VkDeviceAddress bufferAddress = context.GetBufferBDA(rid);
        //来自vkspec ，这里address 必须能被heapAlignment 对齐:
        //resourceHeapAlignment specifies the required alignment of the heapRange->address member of VkBindHeapInfoEXT for binding resource heaps. 
        // It must be a power-of-two value.
        bindInfo.heapRange.address = ALIGN_UP(bufferAddress, heapAlignment);
        mapped = static_cast<char*>(context.GetBufferMappedPtr(rid)) + (bindInfo.heapRange.address - bufferAddress);
        return rid;
    }

    RID DescriptorSlot::GenerateSlotBuffer(Context& context) {
        BufferCreateInfo createInfo{};
        createInfo.hostVisible = true;
        createInfo.size = DESCRIPTOR_SLOT_BLOCK_SIZE * DESCRIPTOR_MAX_SLOT_COUNT;
        createInfo.usage = VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT;
        RID rid = context.CreateBuffer(createInfo);

        m_slotBufferMapped = static_cast<char*>(context.GetBufferMappedPtr(rid));
        m_slotBufferAddress = context.GetBufferBDA(rid);
        return rid;
    }

    RID DescriptorSlot::GenerateResourceHeapBuffer(Context& context) {
        m_bufferOffset = 0;
        m_textureOffset = static_cast<uint32_t>(ALIGN_UP(DESCRIPTOR_MAX_BUFFER_COUNT * m_bufferDescriptorSize, m_imageDescriptorSize)); //图像在ResourceHeap中的偏移
        const VkDeviceSize reservedOffset = ALIGN_UP(m_textureOffset + DESCRIPTOR_MAX_IMAGE_COUNT * m_imageDescriptorSize,
            std::max(m_bufferDescriptorAlignment, m_imageDescriptorAlignment)); //保留区约定放到末尾

        m_resourceHeapBindInfo = {};
        m_resourceHeapBindInfo.sType = VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT;
        m_resourceHeapBindInfo.heapRange.size = reservedOffset + m_resourceHeapReservedSize;
        m_resourceHeapBindInfo.reservedRangeOffset = reservedOffset;
        m_resourceHeapBindInfo.reservedRangeSize = m_resourceHeapReservedSize;
        return GenerateDescriptorHeapBuffer(context, m_resourceHeapAlignment, m_resourceHeapBindInfo, m_resourceHeapMapped);
    }

    void DescriptorSlot::CreateAllSampler() { //创建所有枚举变量的enum
        //直接塞
        // 1. 先创建DescriptorHeap, Sampler 之后是给驱动使用的保留区
        // 计算Sampler大小
        const VkDeviceSize samplerSize = ALIGN_UP(m_samplerDescriptorSize * static_cast<uint32_t>(SamplerType::COUNT), m_samplerDescriptorAlignment);
        m_samplerHeapBindInfo = {};
        m_samplerHeapBindInfo.sType = VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT;
        m_samplerHeapBindInfo.heapRange.size = samplerSize + m_samplerHeapReservedSize;
        m_samplerHeapBindInfo.reservedRangeOffset = samplerSize;
        m_samplerHeapBindInfo.reservedRangeSize = m_samplerHeapReservedSize;
        m_samplerHeapBuffer = GenerateDescriptorHeapBuffer(*m_context, m_samplerHeapAlignment, m_samplerHeapBindInfo, m_samplerHeapMapped);

        // 2. 创建Sampler并且绑定
        for (uint32_t i = 0; i < static_cast<uint32_t>(SamplerType::COUNT); i++) {
            SamplerType samplerType = static_cast<SamplerType>(i);
            VkSamplerCreateInfo createInfo = CreateSamplerCreateInfo(samplerType);
            VkHostAddressRangeEXT dst{};
            dst.address = m_samplerHeapMapped + i * m_samplerDescriptorSize;
            dst.size = m_samplerDescriptorSize;
            VULKAN_CHECK(m_core->GetVkWriteSamplerDescriptorsEXT()(m_device, 1, &createInfo, &dst));
        }
    }

    void DescriptorSlot::DestroyAllSampler() { 
        m_context->DestroyBuffer(m_samplerHeapBuffer);
        m_samplerHeapBuffer = RID::Invalid();
        m_samplerHeapMapped = nullptr;
        m_samplerHeapBindInfo = {};
    }
}
