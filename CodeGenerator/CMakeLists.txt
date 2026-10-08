file(GLOB_RECURSE CODE_GENERATOR_SOURCES "*.cpp" "*.h")

find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

add_library(CodeGenerator STATIC ${CODE_GENERATOR_SOURCES})

target_include_directories(CodeGenerator PUBLIC
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)

target_link_libraries(CodeGenerator PUBLIC
    clangTooling
    clangFrontend
    clangAST
    clangBasic
    clangASTMatchers
    clangLex
    clangSema
    clangDriver
    clangParse
    clangSerialization
    clangEdit
    clangRewrite
    clangAnalysis
    LLVM
)

target_include_directories(RHI PUBLIC ${CMAKE_SOURCE_DIR})