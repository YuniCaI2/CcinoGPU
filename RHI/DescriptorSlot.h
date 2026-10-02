#pragma once
#include "RHI/RHIStruct.h"
#include "RHI/Core.h"

#include <vector>
#include <cstdint>
#include <unordered_map>
#include <vulkan/vulkan.h>

//这里依赖Descriptor Heap 和 BDA 共同实现资源Bindless 架构
//Buffer 使用BDA
// Texture and Sampler 使用Descriptor Heap
// Push Constant直接推送索引

namespace Ccino::RHI {
    class Context;

    //压成紧凑的Descriptor包, 但是不能超过256字节
    //使用两种策略， 当对象比较小时使用直接PushConstant的策略，比较大的话就直接使用BDA间接索引(> 128字节下)
    struct PushConstantPackage {
        std::vector<uint64_t> bufferRef; 
        std::vector<uint32_t> textureOffsets;
        std::vector<uint32_t> samplerOffsets;
    };

    struct PushConstantPackageIndirect {
        uint64_t m_pushConstantPackageBDA = 0;
    };

    struct Descriptor {
        VkDescriptorType type;
        uint64_t bda;
        uint32_t offset;
    };

    using DescriptorSlotLayout = std::vector<std::pair<std::string, Descriptor>>;

    class DescriptorSlot {
    public:
        bool IsIndirectPushConstant(const DescriptorSlotLayout& layout) const;

        void Init(Context* context);
        void Destroy();

        DescriptorSlot& AddBuffer(const std::string& name, RID rid);
        DescriptorSlot& AddTexture(const std::string& name, RID rid, VkImageSubresourceRange subresourceRange); //这不用我手动管理ImageView
        DescriptorSlot& AddSampler(const std::string& name, SamplerType type);

        DescriptorSlot& DeleteBuffer(RID rid);
        DescriptorSlot& DeleteTexture(RID rid);

        void GeneratePushConstantPackageBuffer(Context& context);

        void BindLayout(const DescriptorSlotLayout& layout);
    private:
        RID GenerateDescriptorHeapBuffer(Context& context, uint32_t size);
        void CreateAllSampler(); //创建所有枚举变量的enum
        void DestroyAllSampler(); //销毁所有枚举变量的enum

        RID m_resourceHeapBuffer = RID::Invalid();
        RID m_samplerHeapBuffer = RID::Invalid();

        //CPU端做映射
        std::vector<RID> m_resourceRIDList;
        std::vector<RID> m_bufferRIDList;
        std::unordered_map<RID, uint32_t> m_resourceOffsetMap; //快速查找
        std::unordered_map<RID, uint32_t> m_bufferOffsetMap;

        //已经释放的队列用来记录对象释放情况方便O1 读取空闲的槽
        std::queue<uint32_t> m_resourceReleaseQueue;
        std::queue<uint32_t> m_bufferReleaseQueue;
        //TODO: 确认是否需要使用锁保证线程安全

        PushConstantPackage m_pushConstantPackage;

        PushConstantPackageIndirect m_pushConstantPackageIndirect; //一直存在
        RID m_pushConstantPackageRID = RID::Invalid(); //VkBuffer 用来管理间接索引

        //Push Constant
        DescriptorSlotLayout m_descriptorSlotLayout;
        char* m_pushConstantData = nullptr;
        std::unordered_map<std::string, uint32_t> m_pushConstantOffsetMap; //用来记录Push Constant的偏移量



        Context* m_context = nullptr;
        Core* m_core = nullptr;
        VkDevice m_device = VK_NULL_HANDLE;
        VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;

        VkDeviceSize m_samplerDescriptorSize = 0;
        VkDeviceSize m_samplerDescriptorAlignment = 0;
        VkDeviceSize m_samplerDescriptorStride = 0;
        VkDeviceSize m_bufferDescriptorSize = 0;
        VkDeviceSize m_bufferDescriptorAlignment = 0;
        VkDeviceSize m_bufferDescriptorStride = 0;
        VkDeviceSize m_imageDescriptorSize = 0;
        VkDeviceSize m_imageDescriptorAlignment = 0;
        VkDeviceSize m_imageDescriptorStride = 0;

        VkDeviceSize m_reversedDescriptorSize = 0;
    };
}