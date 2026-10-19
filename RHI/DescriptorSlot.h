#pragma once
#include "RHI/Context.h"
#include "RHI/RHIStruct.h"
#include "RHI/Core.h"
#include "ShaderStruct/UniformData.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
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
    class DescriptorSlot;

    using Ccino::UniformDataType;
    using Ccino::UniformData;
    using Ccino::DescriptorSlotLayout;
    using Ccino::UniformOffset;
    using Ccino::UniformTypeInfo;
    using Ccino::GetUniformTypeInfo;
    using Ccino::UniformTypeOf;
    using Ccino::UniformTypeOfBase;
    namespace Std140 = Ccino::Std140;

    //一个Binding 对应一次PushConstant, 只能移动不能拷贝, 同一时间只能被一个线程使用
    //Bind* 只修改CPU 端的副本, Push 时才拷贝到当前飞行帧的UniformBuffer, 已经录制的命令不受之后Bind* 的影响
    class DescriptorSlotBinding {
    public:
        DescriptorSlotBinding(DescriptorSlotBinding&& other) noexcept;
        DescriptorSlotBinding& operator=(DescriptorSlotBinding&& other) noexcept;
        DescriptorSlotBinding(const DescriptorSlotBinding&) = delete;
        DescriptorSlotBinding& operator=(const DescriptorSlotBinding&) = delete;
        ~DescriptorSlotBinding(); //UniformBuffer 交给Context 按飞行帧延迟释放

        DescriptorSlotBinding& BindBuffer(const std::string& name, RID rid); //写入Buffer 的BDA
        DescriptorSlotBinding& BindSampler(const std::string& name, SamplerType type); //写入Sampler 在全局DescriptorSet 中的数组索引
        //TODO: BindTexture, 等RHI 支持Texture 创建后实现, 写入SampledImage / StorageImage 的数组索引

        //常量绑定, 按layout 中声明的类型检查, 矩阵按std140 逐列写入
        template<typename T>
        DescriptorSlotBinding& BindParam(const std::string& name, const T& param) {
            const UniformOffset& field = FindField(name);
            if (!Std140::IsParamCompatible(UniformTypeOf<T>::value, field.uniformDataType)) {
                throw std::runtime_error("Uniform param type does not match descriptor slot layout: " + name);
            }
            Std140::WriteParam(m_uniformData.data() + field.offset, param);
            MarkDirty();
            return *this;
        }

        //当前飞行帧的UniformBuffer 落后于CPU 副本时先拷贝, 再用PushConstant 推送它的BDA
        //同一帧内多次Push 共用这一帧的UniformBuffer, GPU 读到的是这一帧最后一次Push 的内容, 需要不同的值请用多个Binding
        void Push(VkCommandBuffer commandBuffer);

        uint32_t GetUniformBufferSize() const { return m_uniformBufferSize; }

        //按std140 计算每个字段的偏移, size 返回UniformBuffer 的大小(按16 字节对齐)
        static std::unordered_map<std::string, UniformOffset> GetOffsetsTable(const DescriptorSlotLayout& layout, uint32_t& size);

    private:
        friend class DescriptorSlot;
        DescriptorSlotBinding(DescriptorSlot* slotContext, const DescriptorSlotLayout& layout);

        void Release();
        const UniformOffset& FindField(const std::string& name) const;
        void MarkDirty();

        DescriptorSlot* m_slotContext = nullptr; //指向DescriptorSlot的指针 相当于全局的上下文
        Context* m_context = nullptr;

        std::unordered_map<std::string, UniformOffset> m_uniformOffsets; //字段名到std140 偏移
        std::vector<char> m_uniformData; //CPU 端副本, 大小是m_uniformBufferSize
        uint32_t m_uniformBufferSize = 0;

        std::vector<RID> m_uniformBuffers; //数量和飞行帧一致
        std::vector<VkDeviceAddress> m_uniformBufferAddresses; //每个UniformBuffer 的BDA, Push 时推送
        std::vector<uint8_t> m_frameDirty; //每个飞行帧的UniformBuffer 是否落后于CPU 副本
    };


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

        Context* m_context = nullptr;
        VkDevice m_device = VK_NULL_HANDLE;
        VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;

        std::atomic<uint32_t> m_liveBindingCount{0};

        //TODO: Texture 在SampledImage / StorageImage 数组中的索引分配, 以及按飞行帧延迟回收

        //全局共享, 只支持一个Context
        inline static VkDevice m_globalDevice = VK_NULL_HANDLE;
        inline static VkDescriptorSetLayout m_globalDescriptorSetLayout = VK_NULL_HANDLE;
        inline static VkPipelineLayout m_globalPipelineLayout = VK_NULL_HANDLE;
        inline static VkDescriptorPool m_globalDescriptorPool = VK_NULL_HANDLE;
        inline static std::mutex m_globalDescriptorPoolMutex; //vkAllocateDescriptorSets / vkFreeDescriptorSets 需要外部同步
        inline static std::array<VkSampler, static_cast<size_t>(SamplerType::COUNT)> m_globalSamplers{};
    };
}
