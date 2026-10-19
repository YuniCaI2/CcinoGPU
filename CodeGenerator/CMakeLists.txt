file(GLOB_RECURSE CODE_GENERATOR_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.h")
file(GLOB_RECURSE CODE_GENERATOR_CPP_SOURCES CONFIGURE_DEPENDS "*.cpp")

if(NOT CODE_GENERATOR_CPP_SOURCES)
    set(CODEGEN_DUMMY_CPP "${CMAKE_CURRENT_BINARY_DIR}/codegen_dummy.cpp")
    if(NOT EXISTS "${CODEGEN_DUMMY_CPP}")
        file(WRITE "${CODEGEN_DUMMY_CPP}" "// Auto-generated placeholder when CodeGenerator has only headers\n")
    endif()
    list(APPEND CODE_GENERATOR_SOURCES "${CODEGEN_DUMMY_CPP}")
endif()

add_library(CodeGenerator STATIC ${CODE_GENERATOR_SOURCES})

target_include_directories(CodeGenerator SYSTEM PUBLIC
    ${CMAKE_SOURCE_DIR}/ThirdPart/llvm-project/llvm/include
    ${CMAKE_BINARY_DIR}/ThirdPart/llvm-project/llvm/include
    ${CMAKE_SOURCE_DIR}/ThirdPart/llvm-project/clang/include
    ${CMAKE_BINARY_DIR}/ThirdPart/llvm-project/llvm/tools/clang/include
)
target_include_directories(CodeGenerator PUBLIC
    ${CMAKE_SOURCE_DIR}
)

target_compile_definitions(CodeGenerator PUBLIC
    _SILENCE_CXX23_ALIGNED_UNION_DEPRECATION_WARNING
)

target_link_libraries(CodeGenerator PUBLIC
    nlohmann_json::nlohmann_json
    clangTooling
    clangFrontend
    clangASTMatchers
    clangAST
    clangBasic
    clangLex
    clangSema
    clangDriver
    clangParse
    clangSerialization
    clangEdit
    clangRewrite
    clangAnalysis
)

target_include_directories(RHI PUBLIC ${CMAKE_SOURCE_DIR})