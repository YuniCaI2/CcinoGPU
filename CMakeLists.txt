cmake_minimum_required(VERSION 3.10)

project(CcinoGPU)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BUILD_TESTS ON)
set(RHI_VULKAN ON)


add_executable(CcinoGPU main.cpp)

target_compile_definitions(CcinoGPU PUBLIC
    CCINO_PROJECT_DIR=\"${CMAKE_SOURCE_DIR}\"
    CCINO_COMPILE_COMMAND_JSON_PATH=\"${CMAKE_BINARY_DIR}/compile_commands.json\"
    CCINO_SHADER_DIR=\"${CMAKE_SOURCE_DIR}/.CcinoGPU/Shaders\"
    CCINO_SHADER_HUGE_HEADER_PATH=\"${CMAKE_SOURCE_DIR}/.CcinoGPU/Shaders/HugeHeader.glsl\"
)

enable_testing()
add_subdirectory(ThirdPart)
add_subdirectory(RHI)
add_subdirectory(Core)
add_subdirectory(RG)
add_subdirectory(CodeGenerator)
add_subdirectory(Context)
target_include_directories(RHI PUBLIC ${CMAKE_SOURCE_DIR})
add_subdirectory(Tests)