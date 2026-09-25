file(GLOB_RECURSE TEST_SOURCES "*.cpp")


if(BUILD_TESTS)
    find_package(GTest REQUIRED)
    include(GoogleTest)

    add_executable(Tests ${TEST_SOURCES})
    target_link_libraries(Tests PRIVATE GTest::gtest_main)

    gtest_discover_tests(Tests) # 询问当前文件夹下有多少可用的测试用例
endif()