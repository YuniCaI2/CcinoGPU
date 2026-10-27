#pragma once
//这里实现Vulkan的基础功能
#include <vulkan/vulkan.h>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>

#include "RHI/RHIStruct.h"

#ifdef NDEBUG
    #define USE_VALIDATION_LAYERS 0
#else
    #define USE_VALIDATION_LAYERS 1
#endif

#define VULKAN_CHECK(expr) do { \
    auto result = (expr); \
    if (result != VK_SUCCESS) { \
        throw std::runtime_error("Vulkan error: " #expr); \
    } \
} while (0)

struct SDL_Window;

namespace Ccino::RHI {
    class Core {
    public:
        Core() = default;
        ~Core() = default;

        void Init(const RHISettings& settings);
        void Destroy();

        VkDevice GetVkDevice() const { return m_device; }
        VkPhysicalDevice GetVkPhysicalDevice() const { return m_physicalDevice; }
        VkInstance GetVkInstance() const { return m_instance; }
        VkSurfaceKHR GetSurface() const {return m_surface;};
        VmaAllocator GetVmaAllocator() const { return m_vmaAllocator; }
        //Present的队列和Graphics的队列是一致的，现在在Vulkan Spec中记录的
        uint32_t GetGraphicsQueueFamilyIndex() const { return m_queueFamilyIndices.graphics; }
        uint32_t GetComputeQueueFamilyIndex() const { return m_queueFamilyIndices.compute; }
        uint32_t GetTransferQueueFamilyIndex() const { return m_queueFamilyIndices.transfer; }
        uint32_t GetQueueFamilyCount() const { return m_queueFamilyCount; }
        uint32_t GetQueueFamilyIndex(CommandQueueType queueType) const {
            switch (queueType) {
                case CommandQueueType::COMPUTE:
                    return m_queueFamilyIndices.compute;
                case CommandQueueType::GRAPHICS:
                    return m_queueFamilyIndices.graphics;
                case CommandQueueType::TRANSFER:
                    return m_queueFamilyIndices.transfer;
            }
            return m_queueFamilyIndices.graphics;
        }
        VkQueue GetVkQueue(CommandQueueType queueType) const {
            switch (queueType) {
                case CommandQueueType::COMPUTE:
                    return m_computeQueue;
                case CommandQueueType::GRAPHICS:
                    return m_graphicsQueue;
                case CommandQueueType::TRANSFER:
                    return m_transferQueue;
            }
            return VK_NULL_HANDLE;
        }
        const VkPhysicalDeviceProperties& GetPhysicalDeviceProperties() const { return m_properties; }

    private:
        VkInstance m_instance = VK_NULL_HANDLE;
        VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
        VkDevice m_device = VK_NULL_HANDLE;
        VkSurfaceKHR m_surface = VK_NULL_HANDLE;

        struct {
            uint32_t graphics;
            uint32_t compute;
            uint32_t transfer;
        } m_queueFamilyIndices{};

        VkQueue m_graphicsQueue = VK_NULL_HANDLE;
        VkQueue m_computeQueue = VK_NULL_HANDLE;
        VkQueue m_transferQueue = VK_NULL_HANDLE;
        uint32_t m_queueFamilyCount = 0;

        VmaAllocator m_vmaAllocator = VK_NULL_HANDLE;

        VkPhysicalDeviceFeatures2 m_enabledFeatures{};
        VkPhysicalDeviceProperties2 m_properties2{};
        VkPhysicalDeviceProperties m_properties{};

        //Window 作为Core的一部分
        SDL_Window* window = nullptr; 

#ifndef NDEBUG
        PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT = nullptr;
        PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT = nullptr;
        VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
#endif

    private:
        void CreateInstance(const std::vector<const char*>& requiredExtensions);
        void DestroyInstance();

        void CreateDevice(VkInstance instance, VkPhysicalDeviceFeatures features, const std::vector<const char*>& extensions, void* pNext);
        void DestroyDevice();

        void DestroySurface();

        //Dynamic Function Pointers
        void InitDynamicFunctions();

        void CreateVmaAllocator();
        void DestroyVmaAllocator();

        //Debug
        void SetDebugging(VkInstance instance);
        void DestroyDebugCallback(VkInstance instance);

    };
}
