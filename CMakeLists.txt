cmake_minimum_required(VERSION 3.10)

project(CcinoGPU)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BUILD_TESTS ON)


add_executable(CcinoGPU main.cpp)

enable_testing()
add_subdirectory(Tests)
