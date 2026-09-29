#include "RHI/Context.h"
#include <cstdint>
#include <cstring>
#include <vector>
#include <gtest/gtest.h>

TEST(Buffer, UploadLocalAndReadback) {
    Ccino::RHI::Context context;
    RHISettings settings;
    context.Init(settings);

    std::vector<uint32_t> data(64);
    for (uint32_t i = 0; i < data.size(); ++i) {
        data[i] = i * 3 + 1;
    }
    const uint32_t size = static_cast<uint32_t>(data.size() * sizeof(uint32_t));

    RID local = context.CreateStorageBuffer(size, data.data());
    RID readback = context.CreateReadbackBuffer(size);
    context.ReadbackBuffer(local, readback);

    while (!context.IsBufferAvailable(readback)) {
    }

    auto* mapped = static_cast<uint32_t*>(context.MapBuffer(readback));
    ASSERT_NE(mapped, nullptr);
    EXPECT_EQ(std::memcmp(mapped, data.data(), size), 0);
    context.UnmapBuffer(readback);

    context.DestroyBuffer(readback);
    context.DestroyBuffer(local);
    context.Destroy();
}
