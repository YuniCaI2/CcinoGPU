#pragma once
#include "RHI/Context.h"
#include <gtest/gtest.h>

//整个测试进程共用一个Context, 只在所有测试开始前Init、结束后Destroy 一次
inline Ccino::RHI::Context& GetTestContext() {
    static Ccino::RHI::Context context;
    return context;
}

class TestContextEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        RHISettings settings;
        GetTestContext().Init(settings);
    }

    void TearDown() override {
        GetTestContext().Destroy();
    }
};

inline ::testing::Environment* const g_testContextEnvironment = ::testing::AddGlobalTestEnvironment(new TestContextEnvironment);
