file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "*.cpp")
file(GLOB PASS_SOURCES CONFIGURE_DEPENDS "./Passes/*.cpp")


if(BUILD_TESTS)
    include(GoogleTest)

    add_executable(Tests ${TEST_SOURCES})
    add_library(Passes STATIC ${PASS_SOURCES})
    target_link_libraries(Tests PRIVATE GTest::gtest_main Context RHI RG CodeGenerator GLSLCompiler)
    target_link_libraries(Passes PRIVATE GTest::gtest_main Context RHI RG CodeGenerator GLSLCompiler)

    gtest_discover_tests(Tests) # 询问当前文件夹下有多少可用的测试用例

    target_include_directories(Tests PUBLIC ${CMAKE_SOURCE_DIR})
    target_include_directories(Passes PUBLIC ${CMAKE_SOURCE_DIR})
endif()
