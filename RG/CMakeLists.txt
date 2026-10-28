file(GLOB_RECURSE RGSources "*.h" "*.cpp")

add_library(RG STATIC ${RGSources})

if(CCINO_UNITY_BUILD)
    set_target_properties(RG PROPERTIES
        UNITY_BUILD ON
        UNITY_BUILD_BATCH_SIZE 0
    )
endif()

target_include_directories(RG PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(RG PUBLIC RHI Core)

target_include_directories(RHI PUBLIC ${CMAKE_SOURCE_DIR})