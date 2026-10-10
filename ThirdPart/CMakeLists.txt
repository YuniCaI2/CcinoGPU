set(GLM_BUILD_LIBRARY OFF)
set(GLM_BUILD_TESTS OFF)
set(GLM_BUILD_INSTALL OFF)
add_subdirectory(glm)

if(RHI_VULKAN)
    set(VMA_ENABLE_INSTALL OFF)
    add_subdirectory(VulkanMemoryAllocator)
endif()
