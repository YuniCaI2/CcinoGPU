#include <gtest/gtest.h>
#include <cstdint>
#include <vector>

#include "TestContext.h"
#include "Context/CcinoContext.h"
#include "Core/Buffer.h"
#include "Core/CcinoType.h"
#include "RG/RenderGraph.h"

namespace {
    struct IntegrationWorkItem {
        int lhs;
        int rhs;
    };

    struct alignas(16) IntegrationBufferElem {
        int value;
    };

    struct IntegrationPixelElem {
        Ccino::Vec4 color;
    };

    int EvaluateIntegrationItem(IntegrationWorkItem item, int scale) {
        return (item.lhs + item.rhs) * scale;
    }
} // namespace

TEST(CcinoIntegrationTest, MinimalComputeClosedLoop) {
    // 若同一测试进程中已有全局 TestContext，先销毁以避免 DescriptorSlot 单设备全局限制冲突
    DestroyTestContextIfInitialized();

    // 1. 初始化顶层 Context（加载 .CcinoGPU/Shaders/*.json 与 GlobalHeader.glsl 并编译注册 SPIR-V）
    Ccino::Context ctx;
    RHISettings settings{};
    settings.bUseSwapChain = false;
    settings.flightFrameCount = 1;
    ASSERT_TRUE(ctx.Init(settings));

    // 2. 准备 GPU 资源：transientBuf 为 RDG 内部临时 Buffer，externalOutBuffer 为外部输出 Buffer
    constexpr uint32_t kElementCount = 16;
    constexpr uint32_t kBufferByteSize = kElementCount * sizeof(IntegrationBufferElem);

    Ccino::Ref<Ccino::Buffer> externalOutBuffer = ctx.CreateBuffer(
        Ccino::Buffer::CreateArgs{Ccino::BufferType::Device, kBufferByteSize});
    ASSERT_TRUE(externalOutBuffer.IsValid());

    // 3. 创建 RGBuilder 并录制两个具有 RAW 依赖的 Compute Pass
    Ccino::RGBuilder* builder = ctx.CreateRGBuilder("MinimalClosedLoopGraph");
    ASSERT_NE(builder, nullptr);
    EXPECT_EQ(ctx.GetRGBuilder("MinimalClosedLoopGraph"), builder);

    RID transientBuf = builder->CreateTransientBuffer({Ccino::BufferType::Device, kBufferByteSize});
    RID externalBuf  = builder->CreateExternalBuffer(externalOutBuffer);

    builder->AddPass<"IntegrationProducerPass">([&](Ccino::RGBuilder& rg, RID& passRID) {
        auto out0 = rg.Write<IntegrationBufferElem>(transientBuf, passRID);
        int factor = 2;

        rg.AddCS<Ccino::ThreadGroupSize{16, 1, 1}>([=]() {
            IntegrationWorkItem w1{1, 2};
            IntegrationWorkItem w2{3, 4};
            // (1 + 2) * 2 + (3 + 4) * 2 + int(Ccino::ComputeGlobalID.x) = 20 + int(Ccino::ComputeGlobalID.x)
            out0[Ccino::ComputeGlobalID.x].value = EvaluateIntegrationItem(w1, factor) + EvaluateIntegrationItem(w2, factor) + int(Ccino::ComputeGlobalID.x);
        }, Ccino::WorkGroupSize{1, 1, 1});
    }).AddPass<"IntegrationConsumerPass">([&](Ccino::RGBuilder& rg, RID& passRID) {
        auto in0  = rg.Read<IntegrationBufferElem>(transientBuf, passRID);
        auto out1 = rg.Write<IntegrationBufferElem>(externalBuf, passRID);
        int bias = 10;

        auto cs = [=]() {
            IntegrationWorkItem w{in0[Ccino::ComputeGlobalID.x].value, bias};
            // (20 + int(Ccino::ComputeGlobalID.x) + 10) * 3 = (30 + int(Ccino::ComputeGlobalID.x)) * 3
            out1[Ccino::ComputeGlobalID.x].value = EvaluateIntegrationItem(w, 3);
        };
        rg.AddCS<Ccino::ThreadGroupSize{16, 1, 1}>(cs, Ccino::WorkGroupSize{1, 1, 1});
    });

    // 4. 运行 RenderGraph（拓扑排序 -> 分配 Transient 资源 -> 录制 Barrier/绑定/Dispatch -> 提交队列）
    ASSERT_TRUE(ctx.Run("MinimalClosedLoopGraph"));

    // 5. 读回外部 Buffer 数据并验证 GPU 计算结果
    std::vector<IntegrationBufferElem> resultData(kElementCount);
    externalOutBuffer->Readback(resultData.data());

    for (uint32_t i = 0; i < kElementCount; ++i) {
        const int expected = (30 + static_cast<int>(i)) * 3;
        EXPECT_EQ(resultData[i].value, expected) << "Mismatch at element " << i;
    }

    // 6. 释放外部 Buffer、RGBuilder 与顶层 Context
    externalOutBuffer.Reset();
    EXPECT_TRUE(ctx.DestroyRGBuilder("MinimalClosedLoopGraph"));
    ctx.Destroy();
}

TEST(CcinoIntegrationTest, StorageImageReadWriteClosedLoop) {
    DestroyTestContextIfInitialized();

    // 1. 初始化顶层 Context
    Ccino::Context ctx;
    RHISettings settings{};
    settings.bUseSwapChain = false;
    settings.flightFrameCount = 1;
    ASSERT_TRUE(ctx.Init(settings));

    // 2. 准备 GPU 资源：4x4 Transient StorageImage + 外部输出 Buffer (用于读回验证)
    constexpr uint32_t kWidth = 4;
    constexpr uint32_t kHeight = 4;
    constexpr uint32_t kPixelCount = kWidth * kHeight;
    constexpr uint32_t kBufferByteSize = kPixelCount * sizeof(IntegrationPixelElem);

    Ccino::Ref<Ccino::Buffer> externalOutBuffer = ctx.CreateBuffer(
        Ccino::Buffer::CreateArgs{Ccino::BufferType::Device, kBufferByteSize});
    ASSERT_TRUE(externalOutBuffer.IsValid());

    Ccino::RGBuilder* builder = ctx.CreateRGBuilder("StorageImageClosedLoopGraph");
    ASSERT_NE(builder, nullptr);

    RID storageTex = builder->CreateTransientTexture2D(Ccino::Texture2D::CreateArgs{
        .width = kWidth,
        .height = kHeight,
        .mipLevels = 1,
        .format = Ccino::TextureFormat::RGBA16_Float,
        .usage = Ccino::TextureUsage::Storage
    });
    RID externalBuf = builder->CreateExternalBuffer(externalOutBuffer);

    // 3. 录制三个 Pass：StoreUAV 写入 -> LoadStoreUAV 读改写 -> LoadUAV 读取并写入外部 Buffer
    builder->AddPass<"StorageImageWritePass">([&](Ccino::RGBuilder& rg, RID& passRID) {
        auto outImg = rg.StoreUAV<Ccino::ViewType::Float>(storageTex, passRID);
        float baseVal = 2.0f;

        rg.AddCS<Ccino::ThreadGroupSize{4, 4, 1}>([=]() {
            Ccino::IVec2 coord(int(Ccino::ComputeGlobalID.x), int(Ccino::ComputeGlobalID.y));
            float idxVal = baseVal + float(coord.y * 4 + coord.x);
            outImg.Store(coord, Ccino::Vec4(idxVal, idxVal * 2.0f, idxVal * 3.0f, 1.0f));
        }, Ccino::WorkGroupSize{1, 1, 1});
    }).AddPass<"StorageImageReadModifyWritePass">([&](Ccino::RGBuilder& rg, RID& passRID) {
        auto rwImg = rg.LoadStoreUAV<Ccino::ViewType::Float>(storageTex, passRID);
        Ccino::Vec4 addend(10.0f, 20.0f, 30.0f, 0.5f);

        rg.AddCS<Ccino::ThreadGroupSize{4, 4, 1}>([=]() {
            Ccino::IVec2 coord(int(Ccino::ComputeGlobalID.x), int(Ccino::ComputeGlobalID.y));
            Ccino::Vec4 prev = rwImg.Load(coord);
            rwImg.Store(coord, prev + addend);
        }, Ccino::WorkGroupSize{1, 1, 1});
    }).AddPass<"StorageImageReadToBufferPass">([&](Ccino::RGBuilder& rg, RID& passRID) {
        auto inImg  = rg.LoadUAV<Ccino::ViewType::Float>(storageTex, passRID);
        auto outBuf = rg.Write<IntegrationPixelElem>(externalBuf, passRID);

        rg.AddCS<Ccino::ThreadGroupSize{4, 4, 1}>([=]() {
            Ccino::IVec2 coord(int(Ccino::ComputeGlobalID.x), int(Ccino::ComputeGlobalID.y));
            uint32_t flatIdx = Ccino::ComputeGlobalID.y * 4u + Ccino::ComputeGlobalID.x;
            outBuf[flatIdx].color = inImg.Load(coord);
        }, Ccino::WorkGroupSize{1, 1, 1});
    });

    // 4. 运行 RenderGraph
    ASSERT_TRUE(ctx.Run("StorageImageClosedLoopGraph"));

    // 5. 读回外部 Buffer 数据并逐像素验证 StorageImage 读写结果
    std::vector<IntegrationPixelElem> resultPixels(kPixelCount);
    externalOutBuffer->Readback(resultPixels.data());

    for (uint32_t y = 0; y < kHeight; ++y) {
        for (uint32_t x = 0; x < kWidth; ++x) {
            const uint32_t idx = y * kWidth + x;
            const float idxVal = 2.0f + static_cast<float>(idx);
            EXPECT_FLOAT_EQ(resultPixels[idx].color.x, idxVal + 10.0f) << "Mismatch R at (" << x << ", " << y << ")";
            EXPECT_FLOAT_EQ(resultPixels[idx].color.y, idxVal * 2.0f + 20.0f) << "Mismatch G at (" << x << ", " << y << ")";
            EXPECT_FLOAT_EQ(resultPixels[idx].color.z, idxVal * 3.0f + 30.0f) << "Mismatch B at (" << x << ", " << y << ")";
            EXPECT_FLOAT_EQ(resultPixels[idx].color.w, 1.5f) << "Mismatch A at (" << x << ", " << y << ")";
        }
    }

    // 6. 释放资源
    externalOutBuffer.Reset();
    EXPECT_TRUE(ctx.DestroyRGBuilder("StorageImageClosedLoopGraph"));
    ctx.Destroy();
}
