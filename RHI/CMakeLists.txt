find_package(Vulkan REQUIRED)

file(GLOB RHI_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.h")

add_library(RHI STATIC ${RHI_SOURCES})

if(CCINO_UNITY_BUILD)
    set_target_properties(RHI PROPERTIES
        UNITY_BUILD ON
        UNITY_BUILD_BATCH_SIZE 0
    )
endif()

target_link_libraries(RHI
    PUBLIC
        Vulkan::Vulkan
        GPUOpen::VulkanMemoryAllocator
        glm::glm
        nlohmann_json::nlohmann_json
        imgui::imgui
    PRIVATE
        SDL3::SDL3-static
)
target_include_directories(RHI PUBLIC ${CMAKE_SOURCE_DIR})
