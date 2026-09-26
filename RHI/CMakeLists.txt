if(RHI_VULKAN)
    find_package(Vulkan REQUIRED)
    file (GLOB_RECURSE API_SOURCES "./Vulkan/*.cpp" "./Vulkan/*.h" ) 
endif()

file(GLOB RHI_SOURCES ${API_SOURCES} "*.cpp" "*.h")


add_library(RHI STATIC ${RHI_SOURCES})

if(RHI_VULKAN)
    target_link_libraries(RHI PUBLIC Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator)
endif()

target_include_directories(RHI PUBLIC ${CMAKE_SOURCE_DIR})