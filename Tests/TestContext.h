#pragma once
#include "RHI/Context.h"
#include <gtest/gtest.h>

inline bool& IsTestContextInitialized() {
    static bool initialized = false;
    return initialized;
}

//整个测试进程共用一个Context, 按需在首次调用GetTestContext 时Init、结束后Destroy
inline Ccino::RHI::Context& GetTestContext() {
    static Ccino::RHI::Context context;
    if (!IsTestContextInitialized()) {
        RHISettings settings;
        context.Init(settings);
        IsTestContextInitialized() = true;
    }
    return context;
}

inline void DestroyTestContextIfInitialized() {
    if (IsTestContextInitialized()) {
        GetTestContext().Destroy();
        IsTestContextInitialized() = false;
    }
}

class TestContextEnvironment : public ::testing::Environment {
public:
    void SetUp() override {}

    void TearDown() override {
        DestroyTestContextIfInitialized();
    }
};

inline ::testing::Environment* const g_testContextEnvironment = ::testing::AddGlobalTestEnvironment(new TestContextEnvironment);
