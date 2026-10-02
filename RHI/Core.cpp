#include "RHI/Core.h"
#include <algorithm>
#include <vector>
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vulkan/vulkan_core.h>

namespace Ccino::RHI {
    void Core::Init(const RHISettings& settings) {
        std::vector<const char*> instanceExtensions = {};

        //配置Vulkan Device Features
        VkPhysicalDeviceFeatures deviceFeatures{};
        deviceFeatures.samplerAnisotropy = VK_TRUE;

        VkPhysicalDeviceVulkan11Features features11{};
        features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
        features11.shaderDrawParameters = VK_TRUE;

        VkPhysicalDeviceVulkan12Features features12{};
        features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        features12.timelineSemaphore = VK_TRUE;
        features12.bufferDeviceAddress = VK_TRUE;
        features12.descriptorBindingPartiallyBound = VK_TRUE;
        features12.runtimeDescriptorArray = VK_TRUE;
        features12.descriptorBindingVariableDescriptorCount = VK_TRUE;
        features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
        features12.scalarBlockLayout = VK_TRUE;
        features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;

        VkPhysicalDeviceVulkan13Features features13{};
        features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        features13.synchronization2 = VK_TRUE;
        features13.dynamicRendering = VK_TRUE;

        VkPhysicalDeviceVulkan14Features features14{};
        features14.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;
        features14.maintenance5 = VK_TRUE;

        VkPhysicalDeviceDescriptorHeapFeaturesEXT descriptorHeapFeatures{};
        descriptorHeapFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT;
        descriptorHeapFeatures.descriptorHeap = VK_TRUE;

        features11.pNext = &features12;
        features12.pNext = &features13;
        features13.pNext = &features14;
        features14.pNext = &descriptorHeapFeatures;

        //这里使用Descriptor Heap
        std::vector<const char*> deviceExtensions = {
            VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME
        };

        CreateInstance(instanceExtensions);
        CreateDevice(m_instance, deviceFeatures, deviceExtensions, &features11);
        if (settings.bUseSwapChain) {
            CreateSurface();
        }   
    }

    void Core::Destroy() {
        DestroyVmaAllocator();
        DestroyDevice();
        DestroySurface();
        DestroyDebugCallback(m_instance);
        DestroyInstance();
    }

    //Debug
    VKAPI_ATTR VkBool32 VKAPI_CALL DebugUtilsMessageCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
        //全部发送
        std::cerr << "Validation Layer: " << pCallbackData->pMessage << std::endl;
        return VK_FALSE;
    }


    void SetupDebuggingMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT &debug) {
        debug.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        //接受消息的类型是，一般类型的消息和验证层消息
        debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
        debug.pfnUserCallback = DebugUtilsMessageCallback;
    }


    void Core::CreateInstance(const std::vector<const char*>& requiredExtensions) {
        VkApplicationInfo appInfo = {};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "CcinoGPU";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 4, 1);
        appInfo.pEngineName = "CcinoGPU";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_4;

        std::vector<const char*> supportedExtensions = {};
        uint32_t extensionCount = 0;

        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
        supportedExtensions.resize(extensionCount);
        std::vector<VkExtensionProperties> supportedExtensionsProperties(extensionCount);
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, supportedExtensionsProperties.data());
        for (uint32_t i = 0; i < extensionCount; i++) {
            supportedExtensions.emplace_back(supportedExtensionsProperties[i].extensionName);
        }


        auto instanceExtensions = requiredExtensions;


        VkInstanceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;


        VkDebugUtilsMessengerCreateInfoEXT debugInfo = {};
        if (USE_VALIDATION_LAYERS) {
            SetupDebuggingMessengerCreateInfo(debugInfo);
            createInfo.pNext = &debugInfo;

            //增加Debug的扩展
            instanceExtensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        } 

        //检测外部的扩展是否可用
        for (const auto& extension : requiredExtensions) {
            if (std::find(supportedExtensions.begin(), supportedExtensions.end(), extension) == supportedExtensions.end()) {
                throw std::runtime_error("Extension " + std::string(extension) + " is not supported");
            }
        }

        if (instanceExtensions.size() > 0) {
            createInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
            createInfo.ppEnabledExtensionNames = instanceExtensions.data();
        } 

        const char* validationLayerName = "VK_LAYER_KHRONOS_validation";
        if (USE_VALIDATION_LAYERS) {
            bool validationLayerFound = false;
            uint32_t layoutCount = 0;
            vkEnumerateInstanceLayerProperties(&layoutCount, nullptr);
            std::vector<VkLayerProperties> layoutProperties(layoutCount);
            vkEnumerateInstanceLayerProperties(&layoutCount, layoutProperties.data());
            for (uint32_t i = 0; i < layoutCount; i++) {
                if (strcmp(layoutProperties[i].layerName, validationLayerName) == 0) {
                    createInfo.enabledLayerCount = 1;
                    createInfo.ppEnabledLayerNames = &validationLayerName;
                    validationLayerFound = true;
                    break;
                }
            }
            if (!validationLayerFound) {
                throw std::runtime_error("Validation layer " + std::string(validationLayerName) + " is not supported");
            }
        }

        if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create instance");
        }
    }
    template<class ScoreFunc>
    requires std::is_invocable_v<ScoreFunc, VkPhysicalDevice> && std::is_integral_v<std::invoke_result_t<ScoreFunc, VkPhysicalDevice>> //这里概念是规定函数类型
    VkPhysicalDevice PickPhysicalDevices(VkInstance instance, ScoreFunc scoreFunc) {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
        if (deviceCount == 0) {
            std::cerr << "Failed to find GPUs with Vulkan support!" << std::endl;
            return {};
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
        std::sort(devices.begin(), devices.end(), [&scoreFunc](VkPhysicalDevice a, VkPhysicalDevice b) {
            return scoreFunc(a) > scoreFunc(b);
        });
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(devices[0], &properties);
        std::cout << "Selected GPU: " << properties.deviceName << " (score: " << scoreFunc(devices[0]) << ")" << std::endl;
        return devices[0];
    }


    void Core::CreateDevice(VkInstance instance, VkPhysicalDeviceFeatures features, const std::vector<const char*>& extensions, void* pNext) {
        m_instance = instance;
        auto scoreDevice = [](VkPhysicalDevice device) {
            VkPhysicalDeviceProperties properties;
            vkGetPhysicalDeviceProperties(device, &properties);
            int score = 0;
            if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                score += 1000;
            }else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
                score += 500;
            }else {
                score += 200;
            }
            return score;
        };
        m_physicalDevice = PickPhysicalDevices(instance, scoreDevice);

        const float defaultQueuePriority = 1.0f;

        //得到队列家族
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, queueFamilyProperties.data());

        //获取对应队列Index
        auto GetQueueFamilyIndex = [&](VkQueueFlagBits flags){
            if ((flags & VK_QUEUE_COMPUTE_BIT) == flags) {
                for (uint32_t i = 0; i < queueFamilyProperties.size(); i++) {
                    if ((queueFamilyProperties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) && (
                            queueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) {
                        return i;
                    }
                }
            }

            //找到一个仅传输队列
            if ((flags & VK_QUEUE_TRANSFER_BIT) == flags) {
                for (uint32_t i = 0; i < queueFamilyProperties.size(); i++) {
                    if ((queueFamilyProperties[i].queueFlags & VK_QUEUE_TRANSFER_BIT) && (
                            queueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) {
                        return i;
                    }
                }
            }

            for (uint32_t i = 0; i < static_cast<uint32_t>(queueFamilyProperties.size()); i++) {
                if ((queueFamilyProperties[i].queueFlags & flags) == flags) {
                    return i;
                }
            }

            throw std::runtime_error("failed to find a queue family that supports the requested queue flags!");
        };

        //Queue
        m_queueFamilyIndices.graphics = GetQueueFamilyIndex(VK_QUEUE_GRAPHICS_BIT);
        m_queueFamilyIndices.compute = GetQueueFamilyIndex(VK_QUEUE_COMPUTE_BIT);
        m_queueFamilyIndices.transfer = GetQueueFamilyIndex(VK_QUEUE_TRANSFER_BIT);

        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos{};

        auto requestQueueType = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
        //图形队列
        if (requestQueueType & VK_QUEUE_GRAPHICS_BIT) {
            VkDeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = m_queueFamilyIndices.graphics;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &defaultQueuePriority;
            queueCreateInfos.push_back(queueCreateInfo);
        } else {
            m_queueFamilyIndices.graphics = 0;
        }

        //专用计管线
        if (requestQueueType & VK_QUEUE_COMPUTE_BIT) {
            //防止重复创建
            if (m_queueFamilyIndices.compute != m_queueFamilyIndices.graphics) {
                VkDeviceQueueCreateInfo queueCreateInfo{};
                queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                queueCreateInfo.queueFamilyIndex = m_queueFamilyIndices.compute;
                queueCreateInfo.queueCount = 1;
                queueCreateInfo.pQueuePriorities = &defaultQueuePriority;
                queueCreateInfos.push_back(queueCreateInfo);
            }
        } else {
            m_queueFamilyIndices.compute = m_queueFamilyIndices.graphics;
            //和图形管线一致
        }

        //专有的传输队列
        if (requestQueueType & VK_QUEUE_TRANSFER_BIT) {
            //防止重复创建
            if ((m_queueFamilyIndices.transfer != m_queueFamilyIndices.graphics) && (
                    m_queueFamilyIndices.transfer != m_queueFamilyIndices.compute)) {
                VkDeviceQueueCreateInfo queueCreateInfo{};
                queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                queueCreateInfo.queueFamilyIndex = m_queueFamilyIndices.transfer;
                queueCreateInfo.queueCount = 1;
                queueCreateInfo.pQueuePriorities = &defaultQueuePriority;
                queueCreateInfos.push_back(queueCreateInfo);
            }
        } else {
            m_queueFamilyIndices.transfer = m_queueFamilyIndices.graphics;
        }

        std::vector<const char*> deviceExtensions(extensions);
        // deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME); // SwapChain 手动增加

        VkPhysicalDeviceFeatures supportedFeatures{};
        vkGetPhysicalDeviceFeatures(m_physicalDevice, &supportedFeatures);

        const VkBool32* featurePtr = reinterpret_cast<const VkBool32*>(&features);
        const VkBool32* supportedFeaturePtr = reinterpret_cast<const VkBool32*>(&supportedFeatures);

        //保证所有请求的feature都被支持
        constexpr size_t featureCount = sizeof(VkPhysicalDeviceFeatures) / sizeof(VkBool32);
        for (size_t i = 0; i < featureCount; i++) {
            if (featurePtr[i] == VK_TRUE) {
                if (supportedFeaturePtr[i] == VK_FALSE) {
                    std::cout << "Some feature can't be supported" << std::endl;
                    reinterpret_cast<VkBool32*>(&features)[i] = VK_FALSE;
                    continue;
                }
                reinterpret_cast<VkBool32*>(&features)[i] = VK_TRUE;
            }
        }

        uint32_t extensionCount = 0;
        vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> availableExtensionProperties(extensionCount);
        vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extensionCount, availableExtensionProperties.data());
        auto extensionsSupported = [&](const char* extensionName) {
            for (const auto& extension : availableExtensionProperties) {
                if (strcmp(extension.extensionName, extensionName) == 0) {
                    return true;
                }
            }
            return false;
        };

        VkDeviceCreateInfo deviceCreateInfo = {};
        deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
        deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        deviceCreateInfo.pEnabledFeatures = &features;

        if (!deviceExtensions.empty()) {
            for (const char* enableExtension : deviceExtensions) {
                if (!extensionsSupported(enableExtension)) {
                    throw std::runtime_error(std::string("failed to find required extension: ") + enableExtension);
                }
            }
            deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
            deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
        }

        if (pNext) {
            m_enabledFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            m_enabledFeatures.pNext = pNext;
            m_enabledFeatures.features = features;
            deviceCreateInfo.pNext = &m_enabledFeatures;
            deviceCreateInfo.pEnabledFeatures = nullptr;
        }

        if (vkCreateDevice(m_physicalDevice, &deviceCreateInfo, nullptr, &m_device) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create device");
        }

        m_queueFamilyCount = queueFamilyCount;
        vkGetDeviceQueue(m_device, m_queueFamilyIndices.graphics, 0, &m_graphicsQueue);
        vkGetDeviceQueue(m_device, m_queueFamilyIndices.compute, 0, &m_computeQueue);
        vkGetDeviceQueue(m_device, m_queueFamilyIndices.transfer, 0, &m_transferQueue);

        m_properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        m_descriptorBufferProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT;
        m_descriptorBufferProps.pNext = m_properties2.pNext;
        m_properties2.pNext = &m_descriptorBufferProps;
        vkGetPhysicalDeviceProperties2(m_physicalDevice, &m_properties2);
        m_properties = m_properties2.properties;

        InitDynamicFunctions();
        CreateVmaAllocator();
    }

    void Core::DestroyDevice() {
        m_graphicsQueue = VK_NULL_HANDLE;
        m_computeQueue = VK_NULL_HANDLE;
        m_transferQueue = VK_NULL_HANDLE;
        if (m_device != VK_NULL_HANDLE) {
            vkDestroyDevice(m_device, nullptr);
            m_device = VK_NULL_HANDLE;
        }
    }

    void Core::DestroyInstance() {
        if (m_instance != VK_NULL_HANDLE) {
            vkDestroyInstance(m_instance, nullptr);
            m_instance = VK_NULL_HANDLE;
        }
    }

    void Core::CreateSurface() {
        // Surface 依赖窗口系统，随 SwapChain 一起补
    }

    void Core::DestroySurface() {
        if (m_surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
            m_surface = VK_NULL_HANDLE;
        }
    }

    void Core::CreateVmaAllocator() {
        VmaAllocatorCreateInfo allocatorInfo = {};
        allocatorInfo.physicalDevice = m_physicalDevice;
        allocatorInfo.device = m_device;
        allocatorInfo.instance = m_instance;
        allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT |
            VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT |
            VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE5_BIT;
        allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_4;
        if (vmaCreateAllocator(&allocatorInfo, &m_vmaAllocator) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create VMA allocator");
        }
    }

    void Core::DestroyVmaAllocator() {
        if (m_vmaAllocator != VK_NULL_HANDLE) {
            vmaDestroyAllocator(m_vmaAllocator);
            m_vmaAllocator = VK_NULL_HANDLE;
        }
    }

    void Core::InitDynamicFunctions() {
        //动态加载 VK_EXT_descriptor_buffer 扩展函数
        m_vkGetPhysicalDeviceDescriptorSizeEXT = reinterpret_cast<PFN_vkGetPhysicalDeviceDescriptorSizeEXT>(
            vkGetDeviceProcAddr(m_device, "vkGetPhysicalDeviceDescriptorSizeEXT"));

        m_vkWriteResourceDescriptorsEXT = reinterpret_cast<PFN_vkWriteResourceDescriptorsEXT>(
            vkGetDeviceProcAddr(m_device, "vkWriteResourceDescriptorsEXT"));

        m_vkWriteSamplerDescriptorsEXT = reinterpret_cast<PFN_vkWriteSamplerDescriptorsEXT>(
            vkGetDeviceProcAddr(m_device, "vkWriteSamplerDescriptorsEXT"));
    }

    void Core::SetDebugging(VkInstance instance) {
        vkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
        vkDestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");

        if (vkCreateDebugUtilsMessengerEXT != nullptr) {
            throw std::runtime_error("Failed to create debug utils messenger");
        }

        VkDebugUtilsMessengerCreateInfoEXT debugInfo = {};
        SetupDebuggingMessengerCreateInfo(debugInfo);
        if (vkCreateDebugUtilsMessengerEXT(instance, &debugInfo, nullptr, &m_debugMessenger) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create debug utils messenger");
        }

    }

    void Core::DestroyDebugCallback(VkInstance instance) {
#ifndef NDEBUG
        if (m_debugMessenger != VK_NULL_HANDLE && vkDestroyDebugUtilsMessengerEXT != nullptr) {
            vkDestroyDebugUtilsMessengerEXT(instance, m_debugMessenger, nullptr);
            m_debugMessenger = VK_NULL_HANDLE;
        }
#endif
    }
}