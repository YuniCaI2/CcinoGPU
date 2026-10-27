#pragma once
#include "RHI/RHIStruct.h"
#include "ShaderStruct/UniformData.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

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
    using Ccino::UniformCppType;
    using Ccino::UniformCppTypeBase;
    using Ccino::UniformCppType_t;
    using Ccino::VisitUniformDataType;
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
        DescriptorSlotBinding& BindTexture2DUAV(const std::string& name, RID rid, VkImageViewCreateInfo viewCreateInfo); //StorageTexture StorageImage
        DescriptorSlotBinding& BindTexture2DSRV(const std::string& name, RID rid, VkImageViewCreateInfo viewCreateInfo); //SampledTexture

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

        //运行期按UniformDataType 和原始指针绑定常量(例如从Lambda 闭包偏移提取)
        DescriptorSlotBinding& BindParam(const std::string& name, UniformDataType paramType, const void* paramData) {
            if (paramData == nullptr) {
                throw std::invalid_argument("Uniform param data pointer is null: " + name);
            }
            const UniformOffset& field = FindField(name);
            if (!Std140::IsParamCompatible(paramType, field.uniformDataType)) {
                throw std::runtime_error("Uniform param type does not match descriptor slot layout: " + name);
            }
            Std140::WriteParam(m_uniformData.data() + field.offset, paramType, paramData);
            MarkDirty();
            return *this;
        }

        //当前飞行帧的UniformBuffer 落后于CPU 副本时先拷贝, 再用PushConstant 推送它的BDA
        //同一帧内多次Push 共用这一帧的UniformBuffer, GPU 读到的是这一帧最后一次Push 的内容, 需要不同的值请用多个Binding
        void Push(VkCommandBuffer commandBuffer);

        uint32_t GetUniformBufferSize() const { return m_uniformBufferSize; }

        //按std140 计算每个字段的偏移, size 返回UniformBuffer 的大小(按16 字节对齐)
        static std::unordered_map<std::string, UniformOffset> GetOffsetsTable(const DescriptorSlotLayout& layout, uint32_t& size);

        void Release();
    private:
        friend class DescriptorSlot;
        DescriptorSlotBinding(DescriptorSlot* slotContext, const DescriptorSlotLayout& layout);

        const UniformOffset& FindField(const std::string& name) const;
        void MarkDirty();
        DescriptorSlotBinding& BindTexture2D(const std::string& name, RID rid, VkImageViewCreateInfo viewCreateInfo, bool storage);

        struct BoundTexture {
            bool storage = false;
            RID imageView{};
        };
        std::unordered_map<std::string, BoundTexture> m_boundTextures; //字段名 -> 已绑定的ImageView, 重绑/释放时把旧的延迟回收

        bool isRelease = false;
        DescriptorSlot* m_slotContext = nullptr; //指向DescriptorSlot的指针 相当于全局的上下文
        Context* m_context = nullptr;

        std::unordered_map<std::string, UniformOffset> m_uniformOffsets; //字段名到std140 偏移
        std::vector<char> m_uniformData; //CPU 端副本, 大小是m_uniformBufferSize
        uint32_t m_uniformBufferSize = 0;

        std::vector<RID> m_uniformBuffers; //数量和飞行帧一致
        std::vector<VkDeviceAddress> m_uniformBufferAddresses; //每个UniformBuffer 的BDA, Push 时推送
        std::vector<uint8_t> m_frameDirty; //每个飞行帧的UniformBuffer 是否落后于CPU 副本
    };
}
