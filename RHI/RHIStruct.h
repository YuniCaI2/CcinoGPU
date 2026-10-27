#pragma once
#include <cstdint>
#include <atomic>
#include <functional>
#include <shared_mutex>
#include <mutex>
#include <vector>
#include <queue>
#include <vulkan/vulkan_core.h>


#define ALIGN_UP(value, alignment) (((value) + (alignment) - 1) & ~((alignment) - 1))


struct RHISettings {
    bool bUseSwapChain = false;
    //TODO:增加别的特性
    uint32_t flightFrameCount = 1;

    uint32_t windowHeight = 0;
    uint32_t windowWidth = 0;
};

struct RID {
    uint64_t index = UINT64_MAX;
    uint64_t generation = UINT64_MAX;
    uint64_t type = UINT64_MAX;

    operator bool() const {
        return index != UINT64_MAX && generation != UINT64_MAX && type != UINT64_MAX;
    }

    bool operator ==(const RID& other) const {
        return index == other.index && generation == other.generation && type == other.type;
    }

    uint64_t Hash() const {
        uint64_t h = index;
        h ^= generation + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        h ^= type + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        return h;
    }

    template<typename T>
    static uint64_t GetIndex() {
        static const uint64_t typeIndex = GetCount().fetch_add(1, std::memory_order_relaxed);
        return typeIndex;
    }

    static std::atomic<uint64_t>& GetCount() {
        static std::atomic<uint64_t> count = 0;
        return count;
    }

    static consteval RID Invalid() {
        return RID{UINT64_MAX, UINT64_MAX, UINT64_MAX};
    }
};

template<>
struct std::hash<RID> {
    size_t operator()(const RID& id) const noexcept {
        return static_cast<size_t>(id.Hash());
    }
};

template<typename T>
class TRIDArray {
public:
    struct Slot {
        T value;
        uint64_t generation = 0;
        uint64_t type = RID::GetIndex<T>();
        bool isAlive = false;
    };

    RID Add(const T& value) {
        std::unique_lock lock(m_mutex);
        if (m_freeIndices.empty()) {
            m_data.push_back(Slot{value, 0, RID::GetIndex<T>(), true});
            return RID(m_data.size() - 1, 0, RID::GetIndex<T>());
        } else {
            uint64_t index = m_freeIndices.front();
            m_freeIndices.pop();
            m_data[index].value = value;
            m_data[index].isAlive = true;
            return RID(index, m_data[index].generation, RID::GetIndex<T>());
        }
    }

    RID Add(T&& value) {
        std::unique_lock lock(m_mutex);
        if (m_freeIndices.empty()) {
            m_data.push_back(Slot{std::move(value), 0, RID::GetIndex<T>(), true});
            return RID(m_data.size() - 1, 0, RID::GetIndex<T>());
        } else {
            uint64_t index = m_freeIndices.front();
            m_freeIndices.pop();
            m_data[index].value = std::move(value);
            m_data[index].isAlive = true;
            return RID(index, m_data[index].generation, RID::GetIndex<T>());
        }
    }


    //返回指针
    T* Get(const RID& id) {
        std::shared_lock lock(m_mutex);
        if (id.index >= m_data.size() || !m_data[id.index].isAlive || id.generation != m_data[id.index].generation || id.type != RID::GetIndex<T>()) {
            return nullptr;
        } 
        return &m_data[id.index].value;
    }

    bool Remove(const RID& id) {
        std::unique_lock lock(m_mutex);
        if (id.index >= m_data.size() || !m_data[id.index].isAlive || id.generation != m_data[id.index].generation || id.type != RID::GetIndex<T>()) {
            return false;
        }
        m_data[id.index].isAlive = false;
        m_data[id.index].generation++;
        m_freeIndices.push(id.index);
        return true;
    }

    template<typename Fn>
    void ForEach(Fn&& fn) {
        std::shared_lock lock(m_mutex);
        for (auto& slot : m_data) {
            if (slot.isAlive) {
                fn(slot.value);
            }
        }
    }

    //TODO: 这里的性能有点差，需要有一个更好的性能点
    std::vector<RID> GetAllRIDs() const {
        std::vector<RID> rids;
        for (int i = 0; i < m_data.size(); i++) {
            if (m_data[i].isAlive) {
                rids.push_back(RID(i, m_data[i].generation, m_data[i].type));
            }
        }
        return rids;
    }

    uint64_t Size() const {
        return m_data.size() - m_freeIndices.size();
    }

private:
    std::shared_mutex m_mutex;
    std::vector<Slot> m_data; //数据存储
    std::queue<uint64_t> m_freeIndices; //空闲索引
};




//这里不需要Vertex Buffer 和 Index Buffer 直接使用Bindless 配合 索引完成所有

//纹理先不处理，先把当前的库设置为一个单的计算库
//TODO: 纹理

//区分队列
enum class CommandQueueType {
    COMPUTE = 0,
    GRAPHICS = 1,
    TRANSFER = 2
};

//Pipeline
//先实现计算管线

//Sampler
enum class SamplerType {
    LINEAR_REPEAT = 0,
    LINEAR_CLAMP_TO_EDGE = 1,
    LINEAR_CLAMP_TO_BORDER = 2,
    LINEAR_MIRRORED_REPEAT = 3,
    NEAREST_REPEAT = 4,
    NEAREST_CLAMP_TO_EDGE = 5,
    NEAREST_CLAMP_TO_BORDER = 6,
    NEAREST_MIRRORED_REPEAT = 7,
    COUNT
};

inline VkSamplerCreateInfo CreateSamplerCreateInfo(SamplerType type) {
    VkFilter filter = VK_FILTER_LINEAR;
    VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    switch (type) {
    case SamplerType::LINEAR_REPEAT:
        filter = VK_FILTER_LINEAR;
        mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        break;
    case SamplerType::LINEAR_CLAMP_TO_EDGE:
        filter = VK_FILTER_LINEAR;
        mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        break;
    case SamplerType::LINEAR_CLAMP_TO_BORDER:
        filter = VK_FILTER_LINEAR;
        mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        break;
    case SamplerType::LINEAR_MIRRORED_REPEAT:
        filter = VK_FILTER_LINEAR;
        mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        addressMode = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        break;
    case SamplerType::NEAREST_REPEAT:
        filter = VK_FILTER_NEAREST;
        mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        break;
    case SamplerType::NEAREST_CLAMP_TO_EDGE:
        filter = VK_FILTER_NEAREST;
        mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        break;
    case SamplerType::NEAREST_CLAMP_TO_BORDER:
        filter = VK_FILTER_NEAREST;
        mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        break;
    case SamplerType::NEAREST_MIRRORED_REPEAT:
        filter = VK_FILTER_NEAREST;
        mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        addressMode = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        break;
    default:
        throw std::runtime_error("Invalid sampler type");
    }

    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = filter;
    info.minFilter = filter;
    info.mipmapMode = mipmapMode;
    info.addressModeU = addressMode;
    info.addressModeV = addressMode;
    info.addressModeW = addressMode;
    info.mipLodBias = 0.0f;
    info.anisotropyEnable = VK_TRUE;
    info.maxAnisotropy = 1.0f;
    info.compareEnable = VK_FALSE;
    info.compareOp = VK_COMPARE_OP_NEVER;
    info.minLod = 0.0f;
    info.maxLod = VK_LOD_CLAMP_NONE;
    info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    info.unnormalizedCoordinates = VK_FALSE;
    return info;
}
