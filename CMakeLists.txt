cmake_minimum_required(VERSION 3.16)

project(CcinoGPU)

option(CCINO_UNITY_BUILD "Merge each module's sources into a single unity file" OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BUILD_TESTS ON)
set(RHI_VULKAN ON)


add_executable(CcinoGPU main.cpp)

enable_testing()
add_subdirectory(ThirdPart)
add_compile_definitions(
    CCINO_PROJECT_DIR="${CMAKE_SOURCE_DIR}"
    CCINO_COMPILE_COMMAND_JSON_PATH="${CMAKE_BINARY_DIR}/compile_commands.json"
    CCINO_BINARY_DIR="${CMAKE_BINARY_DIR}/"
    CCINO_SHADER_DIR="${CMAKE_SOURCE_DIR}/.CcinoGPU/Shaders"
    CCINO_GLOBAL_HEADER_FILE="${CMAKE_SOURCE_DIR}/.CcinoGPU/GlobalHeader.glsl"
    CCINO_SHADER_HUGE_HEADER_PATH="${CMAKE_SOURCE_DIR}/.CcinoGPU/Shaders/HugeHeader.glsl"
)
add_subdirectory(RHI)
add_subdirectory(Core)
add_subdirectory(RG)
add_subdirectory(CodeGenerator)
add_subdirectory(Context)
add_subdirectory(GLSLCompiler)
target_include_directories(RHI PUBLIC ${CMAKE_SOURCE_DIR})
add_subdirectory(Tests)
add_subdirectory(Demo)


# 需要理解
add_dependencies(CcinoGPU GenerateShaderIR)
if(TARGET Tests)
    add_dependencies(Tests GenerateShaderIR)
endif()
