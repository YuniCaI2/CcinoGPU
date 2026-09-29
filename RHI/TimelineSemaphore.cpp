#include "RHI/TimelineSemaphore.h"
#include "RHI/Core.h"
#include <stdexcept>

namespace Ccino::RHI {
    TimelineSemaphore::~TimelineSemaphore() {
        Destroy();
    }

    void TimelineSemaphore::Create(VkDevice device) {
        Destroy();
        m_device = device;

        VkSemaphoreTypeCreateInfo typeInfo{};
        typeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        typeInfo.initialValue = 0;

        VkSemaphoreCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        createInfo.pNext = &typeInfo;
        VULKAN_CHECK(vkCreateSemaphore(m_device, &createInfo, nullptr, &m_semaphore));
        m_value = 0;
    }

    void TimelineSemaphore::Destroy() {
        if (m_device != VK_NULL_HANDLE && m_semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device, m_semaphore, nullptr);
        }
        m_semaphore = VK_NULL_HANDLE;
        m_value = 0;
    }

    TimelineSemaphore::Signal TimelineSemaphore::BeginSignal(uint64_t requested) {
        Signal signal;
        signal.lock = std::unique_lock<std::mutex>(m_mutex);
        signal.previous = m_value;
        if (requested == 0) {
            ++m_value;
        } else if (requested <= m_value) {
            throw std::runtime_error("Timeline signal value must be greater than the current value");
        } else {
            m_value = requested;
        }
        signal.current = m_value;
        return signal;
    }

    uint64_t TimelineSemaphore::ReadValue() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_value;
    }

    bool TimelineSemaphore::IsAvailable(uint64_t value) const {
        if (value == 0) {
            return true;
        }
        if (m_semaphore == VK_NULL_HANDLE) {
            return false;
        }

        uint64_t current = 0;
        VULKAN_CHECK(vkGetSemaphoreCounterValue(m_device, m_semaphore, &current));
        return current >= value;
    }

    void TimelineSemaphore::Wait(uint64_t value) const {
        if (m_semaphore == VK_NULL_HANDLE || value == 0 || IsAvailable(value)) {
            return;
        }

        VkSemaphoreWaitInfo waitInfo{};
        waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &m_semaphore;
        waitInfo.pValues = &value;
        VULKAN_CHECK(vkWaitSemaphores(m_device, &waitInfo, UINT64_MAX));
    }
}
