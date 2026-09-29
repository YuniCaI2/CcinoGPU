#pragma once
#include <cstdint>
#include <mutex>
#include <vulkan/vulkan_core.h>

namespace Ccino::RHI {
    class TimelineSemaphore {
    public:
        struct Signal {
            uint64_t previous = 0;
            uint64_t current = 0;
            std::unique_lock<std::mutex> lock;
        };

        TimelineSemaphore() = default;
        ~TimelineSemaphore();

        TimelineSemaphore(const TimelineSemaphore&) = delete;
        TimelineSemaphore& operator=(const TimelineSemaphore&) = delete;
        TimelineSemaphore(TimelineSemaphore&&) = delete;
        TimelineSemaphore& operator=(TimelineSemaphore&&) = delete;

        void Create(VkDevice device);
        void Destroy();

        VkSemaphore GetSemaphore() const { return m_semaphore; }

        // 持有本条 timeline 的锁直到 Signal 析构。vkQueueSubmit2 必须在这之前完成。
        Signal BeginSignal(uint64_t requested = 0);
        uint64_t ReadValue() const;

        bool IsAvailable(uint64_t value) const;
        void Wait(uint64_t value) const;

    private:
        VkDevice m_device = VK_NULL_HANDLE;
        VkSemaphore m_semaphore = VK_NULL_HANDLE;
        uint64_t m_value = 0; //这个作为当前时间信号量的目标
        mutable std::mutex m_mutex;
    };
}
