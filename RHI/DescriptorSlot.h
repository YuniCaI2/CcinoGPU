#pragma once
#include "RHI/RHIStruct.h"
#include "RHI/Core.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <vulkan/vulkan.h>

//这里依赖Descriptor Heap 实现资源Bindless 架构
// Buffer 和 Texture 的Descriptor 写在ResourceHeap, Sampler 写在SamplerHeap, 每个资源(RID) 在堆中只占一个槽位, 所有Binding 共享
// 每个Binding 的内容放在SlotBuffer 的一个固定大小的块里, 每个字段是一个uint32_t, 存放资源在Shader 对应 descriptor_heap 数组中的索引
// PushData 的前8字节是块的设备地址
#define DESCRIPTOR_SLOT_BLOCK_SIZE 256 //每个Binding 块的字节数
#define DESCRIPTOR_MAX_SLOT_COUNT 1024 //块的数量上限, 包括等待延迟释放的块

//提前设置DescriptorHeap的大小
#define DESCRIPTOR_MAX_IMAGE_COUNT 1024 //最大图像数量
#define DESCRIPTOR_MAX_BUFFER_COUNT 1024 //最大缓冲区数量

//着色器中取descriptorHeap使用的是索引
//索引是相对于类型本身的，当取image 那 index = imageOffset / imageDescriptorSize . Buffer 同理

namespace Ccino::RHI {
    class Context;
    class DescriptorSlot;

    struct Descriptor {
        VkDescriptorType type;
    };

    //字段顺序就是块内的顺序, 第i 个字段位于 i * sizeof(uint32_t), 对应Shader 里块结构体(std430)的第i 个uint 成员
    using DescriptorSlotLayout = std::vector<std::pair<std::string, Descriptor>>;

    //一个Binding 对应一次PushData, 只能移动不能拷贝, 同一时间只能被一个线程使用
    class DescriptorSlotBinding {
    public:
        DescriptorSlotBinding(DescriptorSlotBinding&& other) noexcept;
        DescriptorSlotBinding& operator=(DescriptorSlotBinding&& other) noexcept;
        DescriptorSlotBinding(const DescriptorSlotBinding&) = delete;
        DescriptorSlotBinding& operator=(const DescriptorSlotBinding&) = delete;
        ~DescriptorSlotBinding(); //把当前的块交给DescriptorSlot 延迟释放

        DescriptorSlotBinding& BindTexture(const std::string& name, RID rid, VkImageSubresourceRange subresourceRange); //这不用我手动管理ImageView
        DescriptorSlotBinding& BindBuffer(const std::string& name, RID rid);
        DescriptorSlotBinding& BindSampler(const std::string& name, SamplerType type);

        //绑定改过之后换一个新块写入, 旧块延迟释放, GPU 可能还在读的块不会被改写
        void Push(VkCommandBuffer commandBuffer);

    private:
        friend class DescriptorSlot;
        DescriptorSlotBinding(DescriptorSlot* slotContext, DescriptorSlotLayout layout);

        void Release();
        uint32_t FindField(const std::string& name, VkDescriptorType& type) const;

        static constexpr uint32_t MAX_FIELD_COUNT = DESCRIPTOR_SLOT_BLOCK_SIZE / sizeof(uint32_t); //代表能插入多少个index 索引在一个Descriptor的SlotBlock中
        static constexpr uint32_t INVALID_BLOCK = UINT32_MAX;

        DescriptorSlot* m_slotContext = nullptr; //指向DescriptorSlot的指针 相当于全局的上下文
        DescriptorSlotLayout m_layout;
        std::array<uint32_t, MAX_FIELD_COUNT> m_fields{}; //CPU 端的块内容,这里是抽象的不关心数据本身，会被打包上传到PushData

        enum class ResourceType {
            Buffer,
            Texture,
            Sampler,
        };

        struct ResourceEntry {
            RID rid = RID::Invalid();
            ResourceType type = ResourceType::Buffer;
        };

        std::unordered_map<std::string, ResourceEntry> m_fieldMap; //字段名到数据本身的映射,用来处理覆盖
        uint32_t m_block = INVALID_BLOCK;
        bool m_dirty = true;
    };


    class DescriptorSlot {
    public:
        DescriptorSlot() = default;
        DescriptorSlot(const DescriptorSlot&) = delete;
        DescriptorSlot& operator=(const DescriptorSlot&) = delete;

        void Init(Context* context);
        void Update(); //每帧开始、等待这一帧的fence 之后调用, 不能和录制并发
        void Destroy(); //所有Binding 必须先析构

        //线程安全
        DescriptorSlotBinding CreateSlotBinding(DescriptorSlotLayout layout);

        void BindHeaps(VkCommandBuffer commandBuffer) const;

        //资源销毁前调用, 槽位延迟释放, 之后不能再Push 还引用它的Binding
        void ReleaseBuffer(RID rid);
        void ReleaseTexture(RID rid);

    private:
        friend class DescriptorSlotBinding;

        struct HeapEntry {
            uint32_t index = 0;
            VkDescriptorType type = VK_DESCRIPTOR_TYPE_MAX_ENUM;
        };

        //返回资源在Shader 对应 descriptor_heap 数组中的索引, 第一次使用时写入Descriptor
        //这里请求的索引是相对于ResourceHeap 全局的Offset,这里会自动加上Texture本身的Offset和BufferOffset
        uint32_t AcquireBufferIndex(RID rid, VkDescriptorType type);
        uint32_t AcquireTextureIndex(RID rid, VkDescriptorType type, const VkImageSubresourceRange& subresourceRange);
        uint32_t GetSamplerIndex(SamplerType type) const;

        uint32_t AllocateSlotBlock();
        void FreeSlotBlock(uint32_t block);
        char* GetSlotBlockMapped(uint32_t block) const;
        VkDeviceAddress GetSlotBlockAddress(uint32_t block) const;

        RID GenerateDescriptorHeapBuffer(Context& context, VkDeviceSize heapAlignment, VkBindHeapInfoEXT& bindInfo, char*& mapped);
        RID GenerateSlotBuffer(Context& context); //这个尺寸预先分配
        RID GenerateResourceHeapBuffer(Context& context);
        void CreateAllSampler(); //创建所有枚举变量的enum
        void DestroyAllSampler(); //销毁所有枚举变量的enum

        Context* m_context = nullptr;
        Core* m_core = nullptr;
        VkDevice m_device = VK_NULL_HANDLE;
        VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;

        uint32_t m_currentFrame = 0;
        std::atomic<uint32_t> m_liveBindingCount{0};

        //SlotBuffer 按DESCRIPTOR_SLOT_BLOCK_SIZE 分块, 以下状态由m_slotMutex 保护
        std::mutex m_slotMutex;
        RID m_slotBuffer = RID::Invalid();
        char* m_slotBufferMapped = nullptr;
        VkDeviceAddress m_slotBufferAddress = 0;
        uint32_t m_slotBlockCount = 0; //已经切出去过的块数
        std::vector<uint32_t> m_freeSlotBlocks;
        std::vector<std::vector<uint32_t>> m_slotBlockPendingRelease; //按飞行帧延迟释放

        //Mapped 指针和BindInfo 的地址都是按HeapAlignment 对齐后的堆起点
        RID m_resourceHeapBuffer = RID::Invalid();
        char* m_resourceHeapMapped = nullptr;
        VkBindHeapInfoEXT m_resourceHeapBindInfo{};
        RID m_samplerHeapBuffer = RID::Invalid();
        char* m_samplerHeapMapped = nullptr;
        VkBindHeapInfoEXT m_samplerHeapBindInfo{};

        //ResourceHeap 中Buffer 段和Image 段的起点, 必须是各自DescriptorSize 的整数倍, 索引才能落在数组元素上
        uint32_t m_bufferOffset = 0;
        uint32_t m_textureOffset = 0;

        //以下堆槽位的分配状态由m_heapMutex 保护
        std::mutex m_heapMutex;
        std::vector<std::pair<RID, uint32_t>> m_textureRIDList;
        std::vector<std::pair<RID, uint32_t>> m_bufferRIDList;
        std::unordered_map<RID, HeapEntry> m_textureIndexMap; //快速查找
        std::unordered_map<RID, HeapEntry> m_bufferIndexMap;
        //已经释放的队列用来记录对象释放情况方便O1 读取空闲的槽
        std::queue<uint32_t> m_textureReleaseQueue;
        std::queue<uint32_t> m_bufferReleaseQueue;
        std::vector<std::vector<uint32_t>> m_texturePendingRelease; //按飞行帧延迟释放 这里相当于是一个桶， 这个桶用来存储各个需要释放的帧
        std::vector<std::vector<uint32_t>> m_bufferPendingRelease;

        //Shader 中 descriptor_heap 数组的步长就是DescriptorSize
        //spec中记录Alignment 是要小于或者等于Size,并且size和alignment 必须是2 的幂
        VkDeviceSize m_samplerDescriptorSize = 0;
        VkDeviceSize m_samplerDescriptorAlignment = 0;
        VkDeviceSize m_bufferDescriptorSize = 0;
        VkDeviceSize m_bufferDescriptorAlignment = 0;
        VkDeviceSize m_imageDescriptorSize = 0;
        VkDeviceSize m_imageDescriptorAlignment = 0;

        VkDeviceSize m_resourceHeapAlignment = 0;
        VkDeviceSize m_samplerHeapAlignment = 0;
        VkDeviceSize m_resourceHeapReservedSize = 0;
        VkDeviceSize m_samplerHeapReservedSize = 0;
    };
}
