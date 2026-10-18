#pragma once
#include "RHI/Context.h"
#include "RHI/RHIStruct.h"
#include "RHI/Core.h"

#include <glm/glm.hpp> //Core/CcinoType.h 中的Vec / Mat 都是glm 类型的别名

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
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

    enum class UniformDataType {
        //Resource
        Buffer, //Buffer全使用BDA uint64_t
        SampledImage, //全局DescriptorSet 中的数组索引 uint32_t
        StorageImage, //全局DescriptorSet 中的数组索引 uint32_t
        Sampler, //全局DescriptorSet 中的数组索引 uint32_t

        //Param 标量
        Bool, //GLSL bool 占4字节, BindParam 传入bool 时自动展开成uint32_t
        Int, //int32_t
        UInt, //uint32_t
        Float, //float
        Double, //double

        //Param 向量 对应Core/CcinoType.h
        BVec2, //GLSL bvec 每个分量4字节, glm::bvec 每个分量1字节, BindParam 会逐分量展开成uint32_t
        BVec3,
        BVec4,
        IVec2, //IVec2
        IVec3, //IVec3
        IVec4, //IVec4
        UVec2, //UVec2
        UVec3, //UVec3
        UVec4, //UVec4
        Vec2, //Vec2
        Vec3, //Vec3
        Vec4, //Vec4
        DVec2, //DVec2
        DVec3, //DVec3
        DVec4, //DVec4

        //Param 矩阵 (列主序, MatCxR: C列R行)
        Mat2, //Mat2 / Mat2x2
        Mat3, //Mat3 / Mat3x3
        Mat4, //Mat4 / Mat4x4
        Mat2x3,
        Mat2x4,
        Mat3x2,
        Mat3x4,
        Mat4x2,
        Mat4x3,

        DMat2, //DMat2 / DMat2x2
        DMat3, //DMat3 / DMat3x3
        DMat4, //DMat4 / DMat4x4
        DMat2x3,
        DMat2x4,
        DMat3x2,
        DMat3x4,
        DMat4x2,
        DMat4x3,
    };

    //单个Uniform 成员在std140 下的布局信息
    struct UniformTypeInfo {
        uint32_t size = 0;         //成员在Buffer 中占用的字节数, 矩阵包含列间padding
        uint32_t alignment = 0;    //基础对齐, 成员偏移必须是它的整数倍
        uint32_t columns = 1;      //矩阵列数C, 非矩阵为1
        uint32_t rows = 1;         //向量分量数 / 矩阵行数R, 标量为1
        uint32_t scalarSize = 4;   //单个分量的字节数, double 为8
        uint32_t columnStride = 0; //矩阵相邻两列的间距, 非矩阵等于size
    };

    //UniformBuffer 按std140 排布, Shader 里的Slot 结构体需要声明为std140
    namespace Std140 {
        constexpr uint32_t AlignUp(uint32_t value, uint32_t alignment) {
            return (value + alignment - 1) / alignment * alignment;
        }

        //标量: 大小和对齐都是N
        constexpr UniformTypeInfo Scalar(uint32_t scalarSize) {
            return {scalarSize, scalarSize, 1, 1, scalarSize, scalarSize};
        }

        //vec2 对齐2N, vec3 / vec4 对齐4N, 大小是分量数 * N (vec3 后面可以紧跟一个标量)
        constexpr UniformTypeInfo Vector(uint32_t components, uint32_t scalarSize) {
            const uint32_t size = components * scalarSize;
            const uint32_t alignment = (components == 2 ? 2 : 4) * scalarSize;
            return {size, alignment, 1, components, scalarSize, size};
        }

        //MatCxR (列主序) 等价于C 个vecR 的数组, std140 中数组步长向上取整到16
        constexpr UniformTypeInfo Matrix(uint32_t columns, uint32_t rows, uint32_t scalarSize) {
            const uint32_t stride = AlignUp(Vector(rows, scalarSize).alignment, 16);
            return {columns * stride, stride, columns, rows, scalarSize, stride};
        }
    }

    constexpr UniformTypeInfo GetUniformTypeInfo(UniformDataType type) {
        using T = UniformDataType;
        switch (type) {
            //Resource
            case T::Buffer:       return Std140::Scalar(8); //BDA uint64_t
            case T::SampledImage: return Std140::Scalar(4); //数组索引 uint32_t
            case T::StorageImage: return Std140::Scalar(4); //数组索引 uint32_t
            case T::Sampler:      return Std140::Scalar(4); //数组索引 uint32_t

            //标量
            case T::Bool:   return Std140::Scalar(4);
            case T::Int:    return Std140::Scalar(4);
            case T::UInt:   return Std140::Scalar(4);
            case T::Float:  return Std140::Scalar(4);
            case T::Double: return Std140::Scalar(8);

            //向量
            case T::BVec2: return Std140::Vector(2, 4);
            case T::BVec3: return Std140::Vector(3, 4);
            case T::BVec4: return Std140::Vector(4, 4);
            case T::IVec2: return Std140::Vector(2, 4);
            case T::IVec3: return Std140::Vector(3, 4);
            case T::IVec4: return Std140::Vector(4, 4);
            case T::UVec2: return Std140::Vector(2, 4);
            case T::UVec3: return Std140::Vector(3, 4);
            case T::UVec4: return Std140::Vector(4, 4);
            case T::Vec2:  return Std140::Vector(2, 4);
            case T::Vec3:  return Std140::Vector(3, 4);
            case T::Vec4:  return Std140::Vector(4, 4);
            case T::DVec2: return Std140::Vector(2, 8);
            case T::DVec3: return Std140::Vector(3, 8);
            case T::DVec4: return Std140::Vector(4, 8);

            //float 矩阵 Matrix(列C, 行R, 分量字节)
            case T::Mat2:   return Std140::Matrix(2, 2, 4);
            case T::Mat3:   return Std140::Matrix(3, 3, 4);
            case T::Mat4:   return Std140::Matrix(4, 4, 4);
            case T::Mat2x3: return Std140::Matrix(2, 3, 4);
            case T::Mat2x4: return Std140::Matrix(2, 4, 4);
            case T::Mat3x2: return Std140::Matrix(3, 2, 4);
            case T::Mat3x4: return Std140::Matrix(3, 4, 4);
            case T::Mat4x2: return Std140::Matrix(4, 2, 4);
            case T::Mat4x3: return Std140::Matrix(4, 3, 4);

            //double 矩阵
            case T::DMat2:   return Std140::Matrix(2, 2, 8);
            case T::DMat3:   return Std140::Matrix(3, 3, 8);
            case T::DMat4:   return Std140::Matrix(4, 4, 8);
            case T::DMat2x3: return Std140::Matrix(2, 3, 8);
            case T::DMat2x4: return Std140::Matrix(2, 4, 8);
            case T::DMat3x2: return Std140::Matrix(3, 2, 8);
            case T::DMat3x4: return Std140::Matrix(3, 4, 8);
            case T::DMat4x2: return Std140::Matrix(4, 2, 8);
            case T::DMat4x3: return Std140::Matrix(4, 3, 8);
        }
        throw std::invalid_argument("Unknown UniformDataType");
    }

    //编译期自检, 和GLSL std140 规则对照
    static_assert(GetUniformTypeInfo(UniformDataType::Vec3).alignment == 16);
    static_assert(GetUniformTypeInfo(UniformDataType::Vec3).size == 12);
    static_assert(GetUniformTypeInfo(UniformDataType::Mat3).size == 48);
    static_assert(GetUniformTypeInfo(UniformDataType::Mat3x2).columnStride == 16);
    static_assert(GetUniformTypeInfo(UniformDataType::Mat2x3).size == 32);
    static_assert(GetUniformTypeInfo(UniformDataType::DVec3).alignment == 32);
    static_assert(GetUniformTypeInfo(UniformDataType::DMat3).size == 96);
    static_assert(GetUniformTypeInfo(UniformDataType::DMat4x2).columnStride == 16);

    //C++ 类型到UniformDataType 的映射, BindParam 用它在运行期检查类型
    //没有特化的类型会在编译期报错, 例如size_t / int64_t
    template<typename T>
    struct UniformTypeOf {
        static_assert(sizeof(T) == 0, "Type is not a supported uniform param type");
    };

    template<UniformDataType V>
    struct UniformTypeOfBase {
        static constexpr UniformDataType value = V;
    };

    //标量. bool 单独处理: C++ 1字节, GLSL 4字节
    template<> struct UniformTypeOf<bool>     : UniformTypeOfBase<UniformDataType::Bool> {};
    template<> struct UniformTypeOf<int32_t>  : UniformTypeOfBase<UniformDataType::Int> {};
    template<> struct UniformTypeOf<uint32_t> : UniformTypeOfBase<UniformDataType::UInt> {};
    template<> struct UniformTypeOf<uint64_t> : UniformTypeOfBase<UniformDataType::Buffer> {}; //BDA, 即VkDeviceAddress
    template<> struct UniformTypeOf<float>    : UniformTypeOfBase<UniformDataType::Float> {};
    template<> struct UniformTypeOf<double>   : UniformTypeOfBase<UniformDataType::Double> {};

    //向量 (与Core/CcinoType.h 中的别名一一对应)
    template<> struct UniformTypeOf<glm::bvec2> : UniformTypeOfBase<UniformDataType::BVec2> {};
    template<> struct UniformTypeOf<glm::bvec3> : UniformTypeOfBase<UniformDataType::BVec3> {};
    template<> struct UniformTypeOf<glm::bvec4> : UniformTypeOfBase<UniformDataType::BVec4> {};
    template<> struct UniformTypeOf<glm::ivec2> : UniformTypeOfBase<UniformDataType::IVec2> {};
    template<> struct UniformTypeOf<glm::ivec3> : UniformTypeOfBase<UniformDataType::IVec3> {};
    template<> struct UniformTypeOf<glm::ivec4> : UniformTypeOfBase<UniformDataType::IVec4> {};
    template<> struct UniformTypeOf<glm::uvec2> : UniformTypeOfBase<UniformDataType::UVec2> {};
    template<> struct UniformTypeOf<glm::uvec3> : UniformTypeOfBase<UniformDataType::UVec3> {};
    template<> struct UniformTypeOf<glm::uvec4> : UniformTypeOfBase<UniformDataType::UVec4> {};
    template<> struct UniformTypeOf<glm::vec2>  : UniformTypeOfBase<UniformDataType::Vec2> {};
    template<> struct UniformTypeOf<glm::vec3>  : UniformTypeOfBase<UniformDataType::Vec3> {};
    template<> struct UniformTypeOf<glm::vec4>  : UniformTypeOfBase<UniformDataType::Vec4> {};
    template<> struct UniformTypeOf<glm::dvec2> : UniformTypeOfBase<UniformDataType::DVec2> {};
    template<> struct UniformTypeOf<glm::dvec3> : UniformTypeOfBase<UniformDataType::DVec3> {};
    template<> struct UniformTypeOf<glm::dvec4> : UniformTypeOfBase<UniformDataType::DVec4> {};

    //float 矩阵 (glm::matCxR: C列R行, 与GLSL 一致)
    template<> struct UniformTypeOf<glm::mat2>   : UniformTypeOfBase<UniformDataType::Mat2> {};
    template<> struct UniformTypeOf<glm::mat3>   : UniformTypeOfBase<UniformDataType::Mat3> {};
    template<> struct UniformTypeOf<glm::mat4>   : UniformTypeOfBase<UniformDataType::Mat4> {};
    template<> struct UniformTypeOf<glm::mat2x3> : UniformTypeOfBase<UniformDataType::Mat2x3> {};
    template<> struct UniformTypeOf<glm::mat2x4> : UniformTypeOfBase<UniformDataType::Mat2x4> {};
    template<> struct UniformTypeOf<glm::mat3x2> : UniformTypeOfBase<UniformDataType::Mat3x2> {};
    template<> struct UniformTypeOf<glm::mat3x4> : UniformTypeOfBase<UniformDataType::Mat3x4> {};
    template<> struct UniformTypeOf<glm::mat4x2> : UniformTypeOfBase<UniformDataType::Mat4x2> {};
    template<> struct UniformTypeOf<glm::mat4x3> : UniformTypeOfBase<UniformDataType::Mat4x3> {};

    //double 矩阵
    template<> struct UniformTypeOf<glm::dmat2>   : UniformTypeOfBase<UniformDataType::DMat2> {};
    template<> struct UniformTypeOf<glm::dmat3>   : UniformTypeOfBase<UniformDataType::DMat3> {};
    template<> struct UniformTypeOf<glm::dmat4>   : UniformTypeOfBase<UniformDataType::DMat4> {};
    template<> struct UniformTypeOf<glm::dmat2x3> : UniformTypeOfBase<UniformDataType::DMat2x3> {};
    template<> struct UniformTypeOf<glm::dmat2x4> : UniformTypeOfBase<UniformDataType::DMat2x4> {};
    template<> struct UniformTypeOf<glm::dmat3x2> : UniformTypeOfBase<UniformDataType::DMat3x2> {};
    template<> struct UniformTypeOf<glm::dmat3x4> : UniformTypeOfBase<UniformDataType::DMat3x4> {};
    template<> struct UniformTypeOf<glm::dmat4x2> : UniformTypeOfBase<UniformDataType::DMat4x2> {};
    template<> struct UniformTypeOf<glm::dmat4x3> : UniformTypeOfBase<UniformDataType::DMat4x3> {};

    namespace Std140 {
        //C++ 传入的类型能否写到layout 声明的成员上
        //SampledImage / StorageImage / Sampler 存的是全局DescriptorSet 中的数组索引, 可以直接用uint32_t 传入
        constexpr bool IsParamCompatible(UniformDataType paramType, UniformDataType layoutType) {
            if (paramType == layoutType) {
                return true;
            }
            return paramType == UniformDataType::UInt &&
                (layoutType == UniformDataType::SampledImage || layoutType == UniformDataType::StorageImage ||
                 layoutType == UniformDataType::Sampler);
        }

        //把一个Param 按std140 写入dst, dst 已经指向成员的偏移
        //标量 / 向量: 直接拷贝; 矩阵: 逐列拷贝, 每列之间按columnStride 留出padding; bool: 展开成uint32_t
        template<typename T>
        void WriteParam(char* dst, const T& value) {
            constexpr UniformTypeInfo info = GetUniformTypeInfo(UniformTypeOf<T>::value);

            if constexpr (std::is_same_v<T, bool>) {
                const uint32_t v = value ? 1u : 0u;
                std::memcpy(dst, &v, sizeof(v));
            } else if constexpr (std::is_same_v<T, glm::bvec2> || std::is_same_v<T, glm::bvec3> || std::is_same_v<T, glm::bvec4>) {
                for (glm::length_t i = 0; i < T::length(); ++i) {
                    const uint32_t v = value[i] ? 1u : 0u;
                    std::memcpy(dst + i * sizeof(uint32_t), &v, sizeof(v));
                }
            } else if constexpr (info.columns > 1) {
                //glm 矩阵是紧密排列的C 个vecR, 每列R * scalarSize 字节
                constexpr uint32_t columnBytes = info.rows * info.scalarSize;
                static_assert(sizeof(T) == info.columns * columnBytes, "glm matrix layout is not tightly packed");
                for (uint32_t c = 0; c < info.columns; ++c) {
                    std::memcpy(dst + c * info.columnStride, &value[static_cast<glm::length_t>(c)], columnBytes);
                }
            } else {
                static_assert(sizeof(T) == info.size, "C++ type size does not match the std140 size");
                std::memcpy(dst, &value, sizeof(T));
            }
        }
    }

    //字段顺序就是UniformBuffer 中成员的顺序, 对应Shader 里Slot 结构体(std140) 的成员声明顺序
    //顺序是CPU 和Shader 之间唯一的约定, 所以用vector 而不是unordered_map
    using DescriptorSlotLayout = std::vector<std::pair<std::string, UniformDataType>>;

    struct UniformOffset {
        uint32_t offset;
        UniformDataType uniformDataType;
    };

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
