
file(GLOB_RECURSE RGSources "*.h" "*.cpp")

add_library(Context STATIC ${RGSources})

if(CCINO_UNITY_BUILD)
    set_target_properties(Context PROPERTIES
        UNITY_BUILD ON
        UNITY_BUILD_BATCH_SIZE 0
    )
endif()

target_include_directories(Context PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(Context PUBLIC RHI Core RG)

target_include_directories(Context PUBLIC ${CMAKE_SOURCE_DIR})