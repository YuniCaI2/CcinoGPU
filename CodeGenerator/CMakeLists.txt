file(GLOB_RECURSE CODE_GENERATOR_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.h")
file(GLOB_RECURSE CODE_GENERATOR_CPP_SOURCES CONFIGURE_DEPENDS "*.cpp")
list(FILTER CODE_GENERATOR_SOURCES EXCLUDE REGEX "GeneratorMain\\.cpp$")
list(FILTER CODE_GENERATOR_CPP_SOURCES EXCLUDE REGEX "GeneratorMain\\.cpp$")

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
    glm::glm
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

##需要理解
add_executable(GeneratorMain GeneratorMain.cpp)
target_link_libraries(GeneratorMain PRIVATE CodeGenerator)

set(CCINO_SHADER_GEN_STAMP "${CMAKE_CURRENT_BINARY_DIR}/shader_ir.stamp")
set(CCINO_SHADER_GEN_INPUTS "")
file(GLOB CCINO_SHADER_GEN_ROOT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/*.cpp"
    "${CMAKE_SOURCE_DIR}/*.h"
    "${CMAKE_SOURCE_DIR}/*.hpp"
)
list(APPEND CCINO_SHADER_GEN_INPUTS ${CCINO_SHADER_GEN_ROOT_SOURCES})

set(CCINO_SHADER_GEN_SKIP_DIRS
    ThirdPart CodeGenerator GLSLCompiler RHI Core RG Context .CcinoGPU
)
file(GLOB CCINO_SHADER_GEN_CHILD_DIRS LIST_DIRECTORIES true "${CMAKE_SOURCE_DIR}/*")
foreach(dir ${CCINO_SHADER_GEN_CHILD_DIRS})
    if(NOT IS_DIRECTORY "${dir}" OR dir STREQUAL "${CMAKE_BINARY_DIR}")
        continue()
    endif()
    get_filename_component(dir_name "${dir}" NAME)
    if(dir_name IN_LIST CCINO_SHADER_GEN_SKIP_DIRS OR dir_name MATCHES "^\\.")
        continue()
    endif()
    file(GLOB_RECURSE dir_sources CONFIGURE_DEPENDS
        "${dir}/*.cpp"
        "${dir}/*.h"
        "${dir}/*.hpp"
    )
    list(APPEND CCINO_SHADER_GEN_INPUTS ${dir_sources})
endforeach()

add_custom_command(
    OUTPUT "${CCINO_SHADER_GEN_STAMP}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_SOURCE_DIR}/.CcinoGPU/Shaders"
    COMMAND "$<TARGET_FILE:GeneratorMain>"
    COMMAND "${CMAKE_COMMAND}" -E touch "${CCINO_SHADER_GEN_STAMP}"
    DEPENDS GeneratorMain "${CMAKE_BINARY_DIR}/compile_commands.json" ${CCINO_SHADER_GEN_INPUTS}
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Generating ShaderIR JSON"
    VERBATIM
)
add_custom_target(GenerateShaderIR ALL DEPENDS "${CCINO_SHADER_GEN_STAMP}")
