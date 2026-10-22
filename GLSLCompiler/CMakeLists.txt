
file(GLOB_RECURSE GLSLCompilerSources "*.h" "*.cpp")

add_library(GLSLCompiler STATIC ${GLSLCompilerSources})

target_include_directories(GLSLCompiler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(GLSLCompiler PUBLIC glslang 
    nlohmann_json::nlohmann_json
)

target_include_directories(GLSLCompiler PUBLIC ${CMAKE_SOURCE_DIR})