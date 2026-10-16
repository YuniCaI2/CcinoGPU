cmake_minimum_required(VERSION 3.10)

project(CcinoGPU)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BUILD_TESTS ON)
set(RHI_VULKAN ON)


add_executable(CcinoGPU main.cpp)

enable_testing()
add_subdirectory(ThirdPart)
add_subdirectory(RHI)
add_subdirectory(Core)
# add_subdirectory(CodeGenerator)
target_include_directories(RHI PUBLIC ${CMAKE_SOURCE_DIR})
add_subdirectory(Tests)