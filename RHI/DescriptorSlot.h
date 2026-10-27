#pragma once
#include "RHI/DescriptorSlotBinding.h"
#include "RHI/RHIStruct.h"
#include "RHI/Core.h"
#include "ShaderStruct/UniformData.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vulkan/vulkan.h>

//Bindless 架构, 只依赖Vulkan 1.2 核心的bufferDeviceAddress 和descriptor_indexing
// Buffer: 全部通过BDA 访问, 不占用Descriptor
// Texture / Sampler: 写在全局DescriptorSet 的descriptor_indexing 数组里, Shader 用数组索引访问 (Texture 暂未实现)
// 每个Binding 有一组UniformBuffer (数量和飞行帧一致), 按std140 存放Buffer 的BDA、Texture / Sampler 的索引和常量参数
// PushConstant 只推送当前飞行帧UniformBuffer 的BDA (8 字节)
// DescriptorSetLayout / PipelineLayout / DescriptorPool 全局唯一, 所有Pipeline 共用

//全局DescriptorSet 中各个数组的binding
#define DESCRIPTOR_BINDING_SAMPLER 0
#define DESCRIPTOR_BINDING_SAMPLED_IMAGE 1
#define DESCRIPTOR_BINDING_STORAGE_IMAGE 2

//各个数组的容量
#define DESCRIPTOR_MAX_SAMPLER_COUNT 16 //不能小于SamplerType::COUNT
#define DESCRIPTOR_MAX_SAMPLED_IMAGE_COUNT 1024
#define DESCRIPTOR_MAX_STORAGE_IMAGE_COUNT 1024

#define DESCRIPTOR_MAX_SET_COUNT 8 //全局DescriptorPool 能分配的DescriptorSet 数量, 每个DescriptorSlot 占一个
#define DESCRIPTOR_PUSH_CONSTANT_SIZE 8 //PushConstant 只有UniformBuffer 的BDA
#define DESCRIPTOR_SHADER_STAGES VK_SHADER_STAGE_COMPUTE_BIT //目前只考虑计算, 支持图形时改成VK_SHADER_STAGE_ALL

static_assert(static_cast<uint32_t>(SamplerType::COUNT) <= DESCRIPTOR_MAX_SAMPLER_COUNT);
static_assert(sizeof(VkDeviceAddress) == DESCRIPTOR_PUSH_CONSTANT_SIZE);

namespace Ccino::RHI {
    class Context;

    class DescriptorSlot {
    public:
        DescriptorSlot() = default;
        DescriptorSlot(const DescriptorSlot&) = delete;
        DescriptorSlot& operator=(const DescriptorSlot&) = delete;

        //全局唯一的DescriptorSetLayout / PipelineLayout / DescriptorPool / Sampler, 由Context::Init / Context::Destroy 调用
        //所有Pipeline 都用同一个PipelineLayout 创建: set 0 是全局DescriptorSet, PushConstant 是UniformBuffer 的BDA
        static void InitGlobal(const Core& core);
        static void DestroyGlobal(); //GPU 必须已经空闲
        static VkDescriptorSetLayout GetGlobalDescriptorSetLayout() { return m_globalDescriptorSetLayout; }
        static VkPipelineLayout GetGlobalPipelineLayout() { return m_globalPipelineLayout; }

        void Init(Context* context); //从全局DescriptorPool 分配DescriptorSet, 写入所有Sampler
        void Update(); //每帧开始、等待这一帧的fence 之后调用, 不能和录制并发
        void Destroy(); //所有Binding 必须先析构, GPU 不能还在使用这个DescriptorSet

        //线程安全
        DescriptorSlotBinding CreateSlotBinding(const DescriptorSlotLayout& layout);

        //每个命令缓冲绑定一次, PipelineLayout 全局唯一, 切换Pipeline 之后不需要重新绑定
        void BindDescriptorSet(VkCommandBuffer commandBuffer) const;

        VkDescriptorSet GetDescriptorSet() const { return m_descriptorSet; }

    private:
        friend class DescriptorSlotBinding;

        uint32_t GetSamplerIndex(SamplerType type) const;
        void WriteAllSampler(); //把全局Sampler 写进这个DescriptorSet 的Sampler 数组, 索引就是SamplerType 的枚举值

        //Texture: 绑定时分配数组索引并写DescriptorSet, 返回Shader 访问用的索引
        uint32_t WriteTextureDescriptor(bool storage, RID imageView);
        //旧View 还可能被飞行中的命令通过旧索引访问, 先放进当前飞行帧的桶, Update 时再真正释放
        void DeferReleaseTexture(bool storage, RID imageView);
        void RecycleTextureBucket(bool storage, uint32_t frameIndex);

        Context* m_context = nullptr;
        VkDevice m_device = VK_NULL_HANDLE;
        VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;

        std::atomic<uint32_t> m_liveBindingCount{0};

        //Texture 描述符数组: 下标就是Shader 访问用的索引, 元素是ImageView 的RID, 空位为Invalid
        std::vector<RID> m_textureUAVArray; //这里直接当做View即可
        std::vector<RID> m_textureSRVArray;
        //Release Bucket: 按飞行帧分桶存放待释放的View, 桶到期后腾出数组索引并销毁View
        std::vector<std::vector<RID>> m_releaseUAVBucket; //当做View即可
        std::vector<std::vector<RID>> m_releaseSRVBucket;

        //全局共享, 只支持一个Context
        inline static VkDevice m_globalDevice = VK_NULL_HANDLE;
        inline static VkDescriptorSetLayout m_globalDescriptorSetLayout = VK_NULL_HANDLE;
        inline static VkPipelineLayout m_globalPipelineLayout = VK_NULL_HANDLE;
        inline static VkDescriptorPool m_globalDescriptorPool = VK_NULL_HANDLE;
        inline static std::mutex m_globalDescriptorPoolMutex; //vkAllocateDescriptorSets / vkFreeDescriptorSets 需要外部同步
        inline static std::array<VkSampler, static_cast<size_t>(SamplerType::COUNT)> m_globalSamplers{};
    };
}
