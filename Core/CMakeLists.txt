file(GLOB_RECURSE CoreSources "*.h" "*.cpp")

add_library(Core STATIC ${CoreSources})

if(CCINO_UNITY_BUILD)
    set_target_properties(Core PROPERTIES
        UNITY_BUILD ON
        UNITY_BUILD_BATCH_SIZE 0
    )
endif()

target_include_directories(Core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(Core PUBLIC glm::glm)
target_link_libraries(Core PUBLIC RHI)

target_include_directories(Core PUBLIC ${CMAKE_SOURCE_DIR})