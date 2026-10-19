find_package(Vulkan REQUIRED)

file(GLOB RHI_SOURCES "*.cpp" "*.h")

add_library(RHI STATIC ${RHI_SOURCES})

target_link_libraries(RHI PUBLIC Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator glm::glm nlohmann_json::nlohmann_json)
target_include_directories(RHI PUBLIC ${CMAKE_SOURCE_DIR})
