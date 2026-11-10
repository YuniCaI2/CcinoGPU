set(GLM_BUILD_LIBRARY OFF)
set(GLM_BUILD_TESTS OFF)
set(GLM_BUILD_INSTALL OFF)
add_subdirectory(glm)

set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
add_subdirectory(json)

if(RHI_VULKAN)
    set(VMA_ENABLE_INSTALL OFF)
    add_subdirectory(VulkanMemoryAllocator)
endif()

if(BUILD_TESTS)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF)
    add_subdirectory(googletest)
endif()

# SDL3 —— 平台 / 窗口层
# 这两个是「构建哪个产物」的开关，不是编译宏；消费方不需要定义 SDL_STATIC 之类的宏。
# 用 CACHE 写入（不 FORCE），这样以后在 CMakeCache 里还能手动切回 shared。
set(SDL_SHARED        OFF CACHE BOOL "Build SDL3 shared library")
set(SDL_STATIC        ON  CACHE BOOL "Build SDL3 static library")
# 不需要 SDL 自带的测试库 / 测试程序 / 示例，省编译时间
set(SDL_TEST_LIBRARY  OFF)
set(SDL_TESTS         OFF)
set(SDL_EXAMPLES      OFF)
add_subdirectory(SDL)

# Dear ImGui —— 调试 UI
# 上游没有可直接 add_subdirectory 的库目标，这里只编核心、SDL3 平台后端和 Vulkan 渲染后端。
# 搜索路径包含仓库根和 backends/，所以可以这样包含：
#   #include "imgui.h"
#   #include "imgui_impl_sdl3.h"
#   #include "imgui_impl_vulkan.h"
# 链接: target_link_libraries(<target> PRIVATE imgui::imgui)
find_package(Vulkan REQUIRED)
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)
add_library(imgui::imgui ALIAS imgui)
target_include_directories(imgui SYSTEM PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
target_link_libraries(imgui PUBLIC
    SDL3::SDL3-static
    Vulkan::Vulkan
)

# LLVM / Clang (LibTooling) 配置
# LLVM/Clang 子模块跟踪 release/23.x；实际版本由父仓库记录的子模块提交决定。
# LLVM 子项目使用其要求的 C++17，避免继承主工程的 C++23。
set(CMAKE_CXX_STANDARD 17)
set(LLVM_ENABLE_PROJECTS "clang" CACHE STRING "" FORCE)
# 使用 "host" 自动匹配当前平台架构（x86_64 Windows/Linux/Mac 为 X86，Apple Silicon/ARM64 Linux 为 AArch64）
set(LLVM_TARGETS_TO_BUILD "host" CACHE STRING "" FORCE)
set(LLVM_ENABLE_RTTI ON CACHE BOOL "" FORCE)
set(LLVM_ENABLE_EH ON CACHE BOOL "" FORCE)
set(LLVM_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(LLVM_INCLUDE_TESTS OFF CACHE BOOL "" FORCE)
set(LLVM_INCLUDE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLVM_INCLUDE_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(LLVM_INCLUDE_DOCS OFF CACHE BOOL "" FORCE)
set(CLANG_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(CLANG_INCLUDE_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory(llvm-project/llvm EXCLUDE_FROM_ALL)

# GLSL
find_package(Python3 COMPONENTS Interpreter REQUIRED)
execute_process(
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/glslang/update_glslang_sources.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/glslang
    RESULT_VARIABLE UPDATE_GLSLANG_RES
)
if(NOT UPDATE_GLSLANG_RES EQUAL 0)
    message(FATAL_ERROR "Failed to run update_glslang_sources.py")
endif()
add_subdirectory(glslang)

