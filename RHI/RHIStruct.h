#pragma once
#include <cstdint>
#include <atomic>
#include <shared_mutex>
#include <mutex>
#include <vector>
#include <queue>



struct RHISettings {
    bool bUseSwapChain = false;
    //TODO:增加别的特性
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


    template<typename T>
    static uint64_t GetIndex() {
        static const uint64_t typeIndex = GetCount().fetch_add(1, std::memory_order_relaxed);
        return typeIndex;
    }

    static std::atomic<uint64_t>& GetCount() {
        static std::atomic<uint64_t> count = 0;
        return count;
    }

    static RID Invalid() {
        return RID{UINT64_MAX, UINT64_MAX, UINT64_MAX};
    }
};

template<typename T>
class TRIDArray {
public:
    struct Slot {
        T value;
        uint64_t generation = UINT64_MAX; //UINT64_MAX 代表当前为空
        uint64_t type = RID::GetIndex<T>();
    };

    RID Add(const T& value) {
        std::unique_lock lock(m_mutex);
        if (m_freeIndices.empty()) {
            m_data.push_back(value);
            return RID(m_data.size() - 1, 0, RID::GetIndex<T>());
        } else {
            m_data[m_freeIndices.front()] = value;
            m_freeIndices.pop();
            return RID(m_freeIndices.front(), 0, RID::GetIndex<T>());
        }
    }

    RID Add(T&& value) {
        std::unique_lock lock(m_mutex);
        m_data.push_back(Slot{std::move(value), 0, RID::GetIndex<T>()});
        return RID(m_data.size() - 1, 0, RID::GetIndex<T>());
    }

    //返回指针
    T* Get(const RID& id) {
        std::shared_lock lock(m_mutex);
        if (id.index >= m_data.size() || id.generation != m_data[id.index].generation || id.type != RID::GetIndex<T>()) {
            return nullptr;
        } 
        return &m_data[id.index].value;
    }

    bool Remove(const RID& id) {
        std::unique_lock lock(m_mutex);
        if (id.index >= m_data.size()) {
            return false;
        }
        m_data[id.index].generation = UINT64_MAX;
        m_freeIndices.push(id.index);
        return true;
    }

    template<typename Fn>
    void ForEach(Fn&& fn) {
        std::shared_lock lock(m_mutex);
        for (auto& slot : m_data) {
            if (slot.generation != UINT64_MAX) {
                fn(slot.value);
            }
        }
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
