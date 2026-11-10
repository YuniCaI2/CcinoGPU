#pragma once
#include <gtest/gtest.h>
#include "Context/CcinoContext.h"


//这个夹具负责处理 ，整体LibTooling的框架构建解析
//这里可以直接整个测试只解析一遍compile_command.json然后直接使用即可
class CppParserTest : public ::testing::Test {
protected:
    //shaderType分为cs vs 和fs
    std::string GetGLSLCode(const std::string& passName, const std::string& shaderType); //负责将测试的头文件

    static void SetUpTestSuite();
private:
    inline static Ccino::Context m_context;
};
