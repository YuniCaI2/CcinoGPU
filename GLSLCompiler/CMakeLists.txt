
file(GLOB_RECURSE GLSLCompilerSources "*.h" "*.cpp")

add_library(GLSLCompiler STATIC ${GLSLCompilerSources})

if(CCINO_UNITY_BUILD)
    set_target_properties(GLSLCompiler PROPERTIES
        UNITY_BUILD ON
        UNITY_BUILD_BATCH_SIZE 0
    )
endif()

target_include_directories(GLSLCompiler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(GLSLCompiler PUBLIC
    glslang
    glslang-default-resource-limits
    SPIRV
    nlohmann_json::nlohmann_json
)

target_include_directories(GLSLCompiler PUBLIC ${CMAKE_SOURCE_DIR})