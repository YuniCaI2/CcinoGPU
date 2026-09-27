#include <gtest/gtest.h>
#include "RHI/RHIStruct.h"

TEST(TRIDArray, Test) {
    TRIDArray<int> array;
    RID id = array.Add(1);
    EXPECT_EQ(*array.Get(id), 1);
    array.Remove(id);
    EXPECT_EQ(array.Get(id), nullptr);
    auto id2 = array.Add(2);
    EXPECT_EQ(array.Get(id), nullptr);
    EXPECT_EQ(*array.Get(id2), 2);
    array.Remove(id2);
    EXPECT_EQ(array.Get(id2), nullptr);
}