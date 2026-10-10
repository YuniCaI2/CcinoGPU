#include <gtest/gtest.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/Tooling.h>
#include <clang-c/Index.h>

TEST(runToolOnCode, CanSyntaxCheckCode) {
  // runToolOnCode returns whether the action was correctly run over the
  // given code.

  EXPECT_TRUE(clang::tooling::runToolOnCode(std::make_unique<clang::SyntaxOnlyAction>(), "class X {};"));
}
