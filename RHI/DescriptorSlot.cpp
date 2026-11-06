#include "RHI/DescriptorSlot.h"
#include "RHI/Context.h"
#include "RHI/Core.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vulkan/vulkan_core.h>

namespace Ccino::RHI {
    namespace {
        //全局DescriptorSet 中所有数组共用的Binding Flags
        // PARTIALLY_BOUND: 没写入的元素只要Shader 不访问就是合法的
        // UPDATE_AFTER_BIND / UPDATE_UNUSED_WHILE_PENDING: DescriptorSet 绑定之后、命令缓冲执行期间, 还能写入没被使用的元素
        constexpr VkDescriptorBindingFlags BINDLESS_BINDING_FLAGS =
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
            VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
            VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;

        struct BindlessArray {
            uint32_t binding;
            VkDescriptorType type;
            uint32_t count;
        };

        //全局DescriptorSet 的布局, 后续支持更多类型时在这里追加
        constexpr BindlessArray BINDLESS_ARRAYS[] = {
            {DESCRIPTOR_BINDING_SAMPLER, VK_DESCRIPTOR_TYPE_SAMPLER, DESCRIPTOR_MAX_SAMPLER_COUNT},
            {DESCRIPTOR_BINDING_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, DESCRIPTOR_MAX_SAMPLED_IMAGE_COUNT},
            {DESCRIPTOR_BINDING_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, DESCRIPTOR_MAX_STORAGE_IMAGE_COUNT},
        };
        constexpr uint32_t BINDLESS_ARRAY_COUNT = static_cast<uint32_t>(std::size(BINDLESS_ARRAYS));

        //检查设备的descriptor_indexing 上限能否容纳全局DescriptorSet
        void CheckBindlessLimits(VkPhysicalDevice physicalDevice) {
            VkPhysicalDeviceDescriptorIndexingProperties indexing{};
            indexing.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES;
            VkPhysicalDeviceProperties2 properties{};
            properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            properties.pNext = &indexing;
            vkGetPhysicalDeviceProperties2(physicalDevice, &properties);

            auto check = [](uint32_t required, uint32_t perStage, uint32_t perSet, const char* what) {
                if (required > perStage || required > perSet) {
                    throw std::runtime_error(std::string("Device does not support enough update-after-bind descriptors: ") + what);
                }
            };
            check(DESCRIPTOR_MAX_SAMPLER_COUNT, indexing.maxPerStageDescriptorUpdateAfterBindSamplers,
                indexing.maxDescriptorSetUpdateAfterBindSamplers, "sampler");
            check(DESCRIPTOR_MAX_SAMPLED_IMAGE_COUNT, indexing.maxPerStageDescriptorUpdateAfterBindSampledImages,
                indexing.maxDescriptorSetUpdateAfterBindSampledImages, "sampled image");
            check(DESCRIPTOR_MAX_STORAGE_IMAGE_COUNT, indexing.maxPerStageDescriptorUpdateAfterBindStorageImages,
                indexing.maxDescriptorSetUpdateAfterBindStorageImages, "storage image");

            uint32_t total = 0;
            for (const BindlessArray& array : BINDLESS_ARRAYS) {
                total += array.count;
            }
            if (total > indexing.maxPerStageUpdateAfterBindResources) {
                throw std::runtime_error("Device does not support enough update-after-bind resources per stage");
            }
        }
    }

    //---------------------------------------------------------------------------------------------
    // DescriptorSlotBinding
    //---------------------------------------------------------------------------------------------

    DescriptorSlotBinding::DescriptorSlotBinding(DescriptorSlot* slotContext, const DescriptorSlotLayout& layout)
        : m_slotContext(slotContext), m_context(slotContext->m_context) {
        uint32_t size = 0;
        m_uniformOffsets = GetOffsetsTable(layout, size);
        m_uniformBufferSize = std::max(size, 8u); //空布局也分配一个最小的块, 保证推送的BDA 有效
        m_uniformData.assign(m_uniformBufferSize, 0);

        //每个Binding 有一组UniformBuffer, 数量和飞行帧一致
        const uint32_t flightFrameCount = m_context->GetFlightFrameCount();
        m_frameDirty.assign(flightFrameCount, 1); //第一次Push 时每一帧都要上传
        m_uniformBuffers.reserve(flightFrameCount);
        m_uniformBufferAddresses.reserve(flightFrameCount);
        try {
            for (uint32_t i = 0; i < flightFrameCount; i++) {
                const RID rid = m_context->CreateUniformBuffer(m_uniformBufferSize, nullptr);
                m_uniformBuffers.push_back(rid);
                const VkDeviceAddress address = m_context->GetBufferBDA(rid);
                if (address % 8 != 0) { //和Shader 中Slot 的buffer_reference_align = 8 对应
                    throw std::runtime_error("Descriptor slot uniform buffer address is not 8-byte aligned");
                }
                m_uniformBufferAddresses.push_back(address);
            }
        } catch (...) {
            //构造失败时析构函数不会执行. 这些Buffer GPU 还没用过, 可以直接销毁
            for (const RID rid : m_uniformBuffers) {
                m_context->DestroyBuffer(rid);
            }
            throw;
        }

        //计数放在构造的最后, 和Release 中的减少成对, 构造失败时不会多出计数
        m_slotContext->m_liveBindingCount.fetch_add(1, std::memory_order_relaxed);
    }

    DescriptorSlotBinding::DescriptorSlotBinding(DescriptorSlotBinding&& other) noexcept
        : m_slotContext(std::exchange(other.m_slotContext, nullptr)),
          m_context(std::exchange(other.m_context, nullptr)),
          m_uniformOffsets(std::move(other.m_uniformOffsets)),
          m_uniformData(std::move(other.m_uniformData)),
          m_uniformBufferSize(std::exchange(other.m_uniformBufferSize, 0)),
          m_uniformBuffers(std::move(other.m_uniformBuffers)),
          m_uniformBufferAddresses(std::move(other.m_uniformBufferAddresses)),
          m_frameDirty(std::move(other.m_frameDirty)),
          m_boundTextures(std::move(other.m_boundTextures)) {
        //被移动的对象m_slotContext 为空, 析构时什么都不释放
    }

    DescriptorSlotBinding& DescriptorSlotBinding::operator=(DescriptorSlotBinding&& other) noexcept {
        if (this != &other) {
            Release();
            m_slotContext = std::exchange(other.m_slotContext, nullptr);
            m_context = std::exchange(other.m_context, nullptr);
            m_uniformOffsets = std::move(other.m_uniformOffsets);
            m_uniformData = std::move(other.m_uniformData);
            m_uniformBufferSize = std::exchange(other.m_uniformBufferSize, 0);
            m_uniformBuffers = std::move(other.m_uniformBuffers);
            m_uniformBufferAddresses = std::move(other.m_uniformBufferAddresses);
            m_frameDirty = std::move(other.m_frameDirty);
            m_boundTextures = std::move(other.m_boundTextures);
        }
        return *this;
    }

    DescriptorSlotBinding::~DescriptorSlotBinding() {
        Release();
    }

    void DescriptorSlotBinding::Release() {
        if (isRelease) return;
        isRelease = true;
        if (m_slotContext == nullptr) {
            return;
        }

        //Texture 的View 可能还在被GPU 通过描述符索引访问, 交给DescriptorSlot 按飞行帧延迟回收
        for (auto& [name, bound] : m_boundTextures) {
            m_slotContext->DeferReleaseTexture(bound.storage, bound.imageView);
        }
        m_boundTextures.clear();

        //UniformBuffer 可能还在被GPU 读取, 交给Context 的延迟释放队列, 等这一帧的飞行帧回来再销毁
        for (const RID rid : m_uniformBuffers) {
            m_context->DeleteResource(rid);
        }
        m_uniformBuffers.clear();
        m_uniformBufferAddresses.clear();
        m_frameDirty.clear();
        m_uniformOffsets.clear();
        m_uniformData.clear();
        m_uniformBufferSize = 0;

        m_slotContext->m_liveBindingCount.fetch_sub(1, std::memory_order_relaxed);
        m_slotContext = nullptr;
        m_context = nullptr;
    }

    std::unordered_map<std::string, UniformOffset> DescriptorSlotBinding::GetOffsetsTable(const DescriptorSlotLayout& layout, uint32_t& size) {
        std::unordered_map<std::string, UniformOffset> offsets;
        offsets.reserve(layout.size());
        uint32_t currentOffset = 0;

        //按声明顺序排布: 先对齐到成员的基础对齐, 再加上成员大小
        for (const auto& [name, uniformType] : layout) {
            const UniformTypeInfo info = GetUniformTypeInfo(uniformType);
            currentOffset = AlignUp(currentOffset, info.alignment);
            if (!offsets.emplace(name, UniformOffset{currentOffset, uniformType}).second) {
                throw std::runtime_error("Duplicate field name in descriptor slot layout: " + name);
            }
            currentOffset += info.size;
        }

        //块整体按8 字节对齐, 尾部补齐
        size = AlignUp(currentOffset, 8);
        return offsets;
    }

    const UniformOffset& DescriptorSlotBinding::FindField(const std::string& name) const {
        auto it = m_uniformOffsets.find(name);
        if (it == m_uniformOffsets.end()) {
            throw std::runtime_error("Field not found in descriptor slot layout: " + name);
        }
        return it->second;
    }

    void DescriptorSlotBinding::MarkDirty() {
        std::fill(m_frameDirty.begin(), m_frameDirty.end(), 1);
    }

    DescriptorSlotBinding& DescriptorSlotBinding::BindBuffer(const std::string& name, RID rid) {
        const UniformOffset& field = FindField(name);
        if (field.uniformDataType != UniformDataType::Buffer) {
            throw std::runtime_error("Field is not a buffer in descriptor slot layout: " + name);
        }

        VkBufferUsageFlags2 usage = 0;
        if (!m_context->GetBufferUsage(rid, usage)) {
            throw std::runtime_error("Buffer resource does not exist: " + name);
        }
        if ((usage & VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT) == 0) {
            throw std::runtime_error("Buffer is not created with shader device address: " + name);
        }

        const uint64_t address = m_context->GetBufferBDA(rid);
        WriteParam(m_uniformData.data() + field.offset, address);
        MarkDirty();
        return *this;
    }

    DescriptorSlotBinding& DescriptorSlotBinding::BindSampler(const std::string& name, SamplerType type) {
        const UniformOffset& field = FindField(name);
        if (field.uniformDataType != UniformDataType::Sampler) {
            throw std::runtime_error("Field is not a sampler in descriptor slot layout: " + name);
        }
        WriteParam(m_uniformData.data() + field.offset, m_slotContext->GetSamplerIndex(type));
        MarkDirty();
        return *this;
    }

    DescriptorSlotBinding& DescriptorSlotBinding::BindTexture2DUAV(const std::string& name, RID rid, const ViewRange& viewRange) {
        return BindTexture2D(name, rid, viewRange, true);
    }

    DescriptorSlotBinding& DescriptorSlotBinding::BindTexture2DSRV(const std::string& name, RID rid, const ViewRange& viewRange) {
        return BindTexture2D(name, rid, viewRange, false);
    }

    DescriptorSlotBinding& DescriptorSlotBinding::BindTexture2D(const std::string& name, RID rid, const ViewRange& viewRange, bool storage) {
        const UniformOffset& field = FindField(name);
        const UniformDataType expected = storage ? UniformDataType::StorageImage : UniformDataType::SampledImage;
        if (field.uniformDataType != expected) {
            throw std::runtime_error(std::string("Field is not a ") + (storage ? "storage" : "sampled") +
                " image in descriptor slot layout: " + name);
        }
        const RID imageView = m_context->CreateTextureView(rid, viewRange);

        uint32_t index = 0;
        try {
            index = m_slotContext->WriteTextureDescriptor(storage, imageView);
        } catch (...) {
            m_context->DestroyImageView(imageView); //还没被GPU 用过, 可以直接销毁
            throw;
        }

        //重绑同一个字段时, 旧View 可能还在被飞行中的命令通过旧索引访问, 交给DescriptorSlot 延迟回收
        if (auto it = m_boundTextures.find(name); it != m_boundTextures.end()) {
            m_slotContext->DeferReleaseTexture(it->second.storage, it->second.imageView);
        }
        m_boundTextures[name] = BoundTexture{storage, imageView};

        WriteParam(m_uniformData.data() + field.offset, index);
        MarkDirty();
        return *this;
    }

    void DescriptorSlotBinding::Push(VkCommandBuffer commandBuffer) {
        if (m_slotContext == nullptr) {
            throw std::runtime_error("Push on a released descriptor slot binding");
        }

        //这一帧的UniformBuffer 落后于CPU 副本时才上传. 没改过就不动它, 避免改写GPU 可能正在读的内容
        const uint32_t frameIndex = m_context->GetCurrentFrameIndex();
        if (m_frameDirty[frameIndex]) {
            const RID uniformBuffer = m_uniformBuffers[frameIndex];
            void* mapped = m_context->MapBuffer(uniformBuffer);
            if (mapped == nullptr) {
                throw std::runtime_error("Failed to map descriptor slot uniform buffer");
            }
            std::memcpy(mapped, m_uniformData.data(), m_uniformBufferSize);
            m_context->UnmapBuffer(uniformBuffer); //非coherent 内存需要flush, 持久映射的Buffer 不会真的Unmap
            m_frameDirty[frameIndex] = 0;
        }

        const VkDeviceAddress address = m_uniformBufferAddresses[frameIndex];
        vkCmdPushConstants(commandBuffer, DescriptorSlot::GetGlobalPipelineLayout(), DESCRIPTOR_SHADER_STAGES,
            0, DESCRIPTOR_PUSH_CONSTANT_SIZE, &address);
    }

    //---------------------------------------------------------------------------------------------
    // DescriptorSlot 全局对象
    //---------------------------------------------------------------------------------------------

    void DescriptorSlot::InitGlobal(const Core& core) {
        const VkDevice device = core.GetVkDevice();
        if (m_globalDevice != VK_NULL_HANDLE) {
            if (m_globalDevice != device) {
                throw std::runtime_error("DescriptorSlot global objects are already created for another device");
            }
            return;
        }
        CheckBindlessLimits(core.GetVkPhysicalDevice());
        m_globalDevice = device;

        try {
            //1. 所有SamplerType 预先创建, 在Sampler 数组中的索引就是枚举值
            for (uint32_t i = 0; i < static_cast<uint32_t>(SamplerType::COUNT); i++) {
                const VkSamplerCreateInfo createInfo = CreateSamplerCreateInfo(static_cast<SamplerType>(i));
                VULKAN_CHECK(vkCreateSampler(device, &createInfo, nullptr, &m_globalSamplers[i]));
            }

            //2. 全局唯一的DescriptorSetLayout, 每种资源一个descriptor_indexing 数组
            VkDescriptorSetLayoutBinding bindings[BINDLESS_ARRAY_COUNT]{};
            VkDescriptorBindingFlags bindingFlags[BINDLESS_ARRAY_COUNT]{};
            for (uint32_t i = 0; i < BINDLESS_ARRAY_COUNT; i++) {
                bindings[i].binding = BINDLESS_ARRAYS[i].binding;
                bindings[i].descriptorType = BINDLESS_ARRAYS[i].type;
                bindings[i].descriptorCount = BINDLESS_ARRAYS[i].count;
                bindings[i].stageFlags = DESCRIPTOR_SHADER_STAGES;
                bindingFlags[i] = BINDLESS_BINDING_FLAGS;
            }

            VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
            bindingFlagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
            bindingFlagsInfo.bindingCount = BINDLESS_ARRAY_COUNT;
            bindingFlagsInfo.pBindingFlags = bindingFlags;

            VkDescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.pNext = &bindingFlagsInfo;
            layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
            layoutInfo.bindingCount = BINDLESS_ARRAY_COUNT;
            layoutInfo.pBindings = bindings;
            VULKAN_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_globalDescriptorSetLayout));

            //3. 全局唯一的PipelineLayout: set 0 是全局DescriptorSet, PushConstant 是UniformBuffer 的BDA
            VkPushConstantRange pushConstantRange{};
            pushConstantRange.stageFlags = DESCRIPTOR_SHADER_STAGES;
            pushConstantRange.offset = 0;
            pushConstantRange.size = DESCRIPTOR_PUSH_CONSTANT_SIZE;

            VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
            pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pipelineLayoutInfo.setLayoutCount = 1;
            pipelineLayoutInfo.pSetLayouts = &m_globalDescriptorSetLayout;
            pipelineLayoutInfo.pushConstantRangeCount = 1;
            pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
            VULKAN_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_globalPipelineLayout));

            //4. 全局DescriptorPool, 每个DescriptorSlot 从这里分配一个DescriptorSet
            VkDescriptorPoolSize poolSizes[BINDLESS_ARRAY_COUNT]{};
            for (uint32_t i = 0; i < BINDLESS_ARRAY_COUNT; i++) {
                poolSizes[i].type = BINDLESS_ARRAYS[i].type;
                poolSizes[i].descriptorCount = BINDLESS_ARRAYS[i].count * DESCRIPTOR_MAX_SET_COUNT;
            }

            VkDescriptorPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            poolInfo.maxSets = DESCRIPTOR_MAX_SET_COUNT;
            poolInfo.poolSizeCount = BINDLESS_ARRAY_COUNT;
            poolInfo.pPoolSizes = poolSizes;
            VULKAN_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_globalDescriptorPool));
        } catch (...) {
            DestroyGlobal();
            throw;
        }
    }

    void DescriptorSlot::DestroyGlobal() {
        if (m_globalDevice == VK_NULL_HANDLE) {
            return;
        }
        //DescriptorPool 销毁时, 从它分配的DescriptorSet 一起释放
        if (m_globalDescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_globalDevice, m_globalDescriptorPool, nullptr);
            m_globalDescriptorPool = VK_NULL_HANDLE;
        }
        if (m_globalPipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_globalDevice, m_globalPipelineLayout, nullptr);
            m_globalPipelineLayout = VK_NULL_HANDLE;
        }
        if (m_globalDescriptorSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(m_globalDevice, m_globalDescriptorSetLayout, nullptr);
            m_globalDescriptorSetLayout = VK_NULL_HANDLE;
        }
        for (VkSampler& sampler : m_globalSamplers) {
            if (sampler != VK_NULL_HANDLE) {
                vkDestroySampler(m_globalDevice, sampler, nullptr);
                sampler = VK_NULL_HANDLE;
            }
        }
        m_globalDevice = VK_NULL_HANDLE;
    }

    //---------------------------------------------------------------------------------------------
    // DescriptorSlot
    //---------------------------------------------------------------------------------------------

    void DescriptorSlot::Init(Context* context) {
        if (m_descriptorSet != VK_NULL_HANDLE) {
            throw std::runtime_error("DescriptorSlot is already initialized");
        }
        if (m_globalDescriptorSetLayout == VK_NULL_HANDLE) {
            throw std::runtime_error("DescriptorSlot::InitGlobal must be called before Init");
        }
        m_context = context;
        m_device = context->GetCore().GetVkDevice();

        {
            std::lock_guard lock(m_globalDescriptorPoolMutex);
            VkDescriptorSetAllocateInfo allocateInfo{};
            allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocateInfo.descriptorPool = m_globalDescriptorPool;
            allocateInfo.descriptorSetCount = 1;
            allocateInfo.pSetLayouts = &m_globalDescriptorSetLayout;
            VULKAN_CHECK(vkAllocateDescriptorSets(m_device, &allocateInfo, &m_descriptorSet));
        }

        //初始化延迟释放桶
        m_releaseSRVBucket.resize(m_context->GetFlightFrameCount());
        m_releaseUAVBucket.resize(m_context->GetFlightFrameCount());

        WriteAllSampler();
    }

    void DescriptorSlot::Update() {
        //每帧开始、等待这一帧的fence 之后调用: 回收当前飞行帧桶里的Texture View 并腾出索引
        if (m_context == nullptr) {
            return;
        }
        const uint32_t frameIndex = m_context->GetCurrentFrameIndex();
        RecycleTextureBucket(false, frameIndex);
        RecycleTextureBucket(true, frameIndex);
    }

    void DescriptorSlot::Destroy() {
        if (m_liveBindingCount.load(std::memory_order_relaxed) != 0) {
            throw std::runtime_error("DescriptorSlot destroyed while bindings are still alive");
        }

        //Binding 释放时View 进了回收桶、还没真正销毁, DescriptorSet 销毁前一起交给Context 释放
        if (m_context != nullptr) {
            for (const RID imageView : m_textureSRVArray) {
                if (imageView) {
                    m_context->DeleteResource(imageView);
                }
            }
            for (const RID imageView : m_textureUAVArray) {
                if (imageView) {
                    m_context->DeleteResource(imageView);
                }
            }
        }
        m_textureSRVArray.clear();
        m_textureUAVArray.clear();
        m_releaseSRVBucket.clear();
        m_releaseUAVBucket.clear();

        //全局DescriptorPool 已经销毁时, DescriptorSet 已经跟着释放了
        if (m_descriptorSet != VK_NULL_HANDLE && m_globalDescriptorPool != VK_NULL_HANDLE) {
            std::lock_guard lock(m_globalDescriptorPoolMutex);
            VULKAN_CHECK(vkFreeDescriptorSets(m_device, m_globalDescriptorPool, 1, &m_descriptorSet));
        }
        m_descriptorSet = VK_NULL_HANDLE;
        m_context = nullptr;
        m_device = VK_NULL_HANDLE;
    }

    DescriptorSlotBinding DescriptorSlot::CreateSlotBinding(const DescriptorSlotLayout& layout) {
        if (m_descriptorSet == VK_NULL_HANDLE) {
            throw std::runtime_error("DescriptorSlot is not initialized");
        }
        //m_liveBindingCount 由DescriptorSlotBinding 的构造函数负责增加
        return DescriptorSlotBinding(this, layout);
    }

    void DescriptorSlot::BindDescriptorSet(VkCommandBuffer commandBuffer ,VkPipelineBindPoint bindPoint) const {
        vkCmdBindDescriptorSets(commandBuffer, bindPoint, m_globalPipelineLayout,
            0, 1, &m_descriptorSet, 0, nullptr);
    }

    uint32_t DescriptorSlot::GetSamplerIndex(SamplerType type) const {
        if (type >= SamplerType::COUNT) {
            throw std::runtime_error("Invalid sampler type");
        }
        return static_cast<uint32_t>(type);
    }

    void DescriptorSlot::WriteAllSampler() {
        std::array<VkDescriptorImageInfo, static_cast<size_t>(SamplerType::COUNT)> imageInfos{};
        for (size_t i = 0; i < imageInfos.size(); i++) {
            imageInfos[i].sampler = m_globalSamplers[i];
        }

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_descriptorSet;
        write.dstBinding = DESCRIPTOR_BINDING_SAMPLER;
        write.dstArrayElement = 0;
        write.descriptorCount = static_cast<uint32_t>(imageInfos.size());
        write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        write.pImageInfo = imageInfos.data();
        vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
    }

    uint32_t DescriptorSlot::WriteTextureDescriptor(bool storage, RID imageView) {
        std::vector<RID>& array = storage ? m_textureUAVArray : m_textureSRVArray;
        const uint32_t maxCount = storage ? DESCRIPTOR_MAX_STORAGE_IMAGE_COUNT : DESCRIPTOR_MAX_SAMPLED_IMAGE_COUNT;

        //优先复用回收腾出的空位, 没有空位再往后追加
        uint32_t index = UINT32_MAX;
        for (uint32_t i = 0; i < static_cast<uint32_t>(array.size()); i++) {
            if (!array[i]) {
                index = i;
                break;
            }
        }
        if (index == UINT32_MAX) {
            if (array.size() >= maxCount) {
                throw std::runtime_error("DescriptorSlot texture descriptor array is full");
            }
            index = static_cast<uint32_t>(array.size());
            array.push_back(RID::Invalid());
        }

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageView = m_context->GetVkImageView(imageView);
        imageInfo.imageLayout = storage ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_descriptorSet;
        write.dstBinding = storage ? DESCRIPTOR_BINDING_STORAGE_IMAGE : DESCRIPTOR_BINDING_SAMPLED_IMAGE;
        write.dstArrayElement = index;
        write.descriptorCount = 1;
        write.descriptorType = storage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);

        array[index] = imageView;
        return index;
    }

    void DescriptorSlot::DeferReleaseTexture(bool storage, RID imageView) {
        if (!imageView || m_context == nullptr) {
            return;
        }
        std::vector<std::vector<RID>>& buckets = storage ? m_releaseUAVBucket : m_releaseSRVBucket;
        if (buckets.empty()) {
            m_context->DeleteResource(imageView);
            return;
        }
        buckets[m_context->GetCurrentFrameIndex()].push_back(imageView);
    }

    void DescriptorSlot::RecycleTextureBucket(bool storage, uint32_t frameIndex) {
        std::vector<std::vector<RID>>& buckets = storage ? m_releaseUAVBucket : m_releaseSRVBucket;
        if (frameIndex >= buckets.size()) {
            return;
        }
        std::vector<RID>& bucket = buckets[frameIndex];
        if (bucket.empty()) {
            return;
        }

        std::vector<RID>& array = storage ? m_textureUAVArray : m_textureSRVArray;
        for (const RID imageView : bucket) {
            //索引先腾出来给后续绑定复用, View 再交给Context 的延迟队列销毁
            for (RID& slot : array) {
                if (slot == imageView) {
                    slot = RID::Invalid();
                }
            }
            m_context->DeleteResource(imageView);
        }
        bucket.clear();
    }
}
