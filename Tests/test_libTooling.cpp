#include <gtest/gtest.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/Tooling.h>
#include <memory>

TEST(runToolOnCode, CanSyntaxCheckCode)
{
    EXPECT_TRUE(clang::tooling::runToolOnCode(std::make_unique<clang::SyntaxOnlyAction>(), "class X {};"));
}
