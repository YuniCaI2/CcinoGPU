file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "*.cpp")


if(BUILD_TESTS)
    include(GoogleTest)

    add_executable(Tests ${TEST_SOURCES})
    target_link_libraries(Tests PRIVATE GTest::gtest_main RHI RG CodeGenerator GLSLCompiler)

    gtest_discover_tests(Tests) # 询问当前文件夹下有多少可用的测试用例

    target_include_directories(Tests PUBLIC ${CMAKE_SOURCE_DIR})
endif()