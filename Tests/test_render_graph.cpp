#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>
#include <gtest/gtest.h>
#include "RG/RenderGraph.h"

namespace {
    // 在不修改 RG/RenderGraph.h 的前提下，通过标准 C++ 友元成员指针显式实例化获取私有成员 m_timeline 与 m_resources
    template <typename Tag, typename Tag::type M>
    struct AccessPrivate {
        friend typename Tag::type GetPrivate(Tag) { return M; }
    };

    struct TimelineTag {
        using type = std::vector<std::vector<RID>> Ccino::RGBuilder::*;
        friend type GetPrivate(TimelineTag);
    };
    template struct AccessPrivate<TimelineTag, &Ccino::RGBuilder::m_timeline>;

    struct ResourcesTag {
        using type = TRIDArray<Ccino::RGResource> Ccino::RGBuilder::*;
        friend type GetPrivate(ResourcesTag);
    };
    template struct AccessPrivate<ResourcesTag, &Ccino::RGBuilder::m_resources>;

    const std::vector<std::vector<RID>>& GetTimeline(const Ccino::RGBuilder& builder) {
        return builder.*GetPrivate(TimelineTag{});
    }

    RID CreateDummyBufferResource(Ccino::RGBuilder& builder, uint32_t size = 256) {
        auto& resources = builder.*GetPrivate(ResourcesTag{});
        return resources.Add(Ccino::RGResource{
            Ccino::ResourceType::Buffer,
            Ccino::Buffer::CreateArgs{Ccino::BufferType::Device, size},
            false
        });
    }

    bool SamePassSet(std::vector<RID> actual, std::vector<RID> expected) {
        if (actual.size() != expected.size()) {
            return false;
        }
        auto cmp = [](const RID& a, const RID& b) {
            if (a.index != b.index) return a.index < b.index;
            if (a.generation != b.generation) return a.generation < b.generation;
            return a.type < b.type;
        };
        std::sort(actual.begin(), actual.end(), cmp);
        std::sort(expected.begin(), expected.end(), cmp);
        return actual == expected;
    }

    std::string FormatTimeline(const std::vector<std::vector<RID>>& timeline) {
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < timeline.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << "Layer" << i << "{";
            for (size_t j = 0; j < timeline[i].size(); ++j) {
                if (j > 0) oss << ", ";
                oss << "P" << timeline[i][j].index;
            }
            oss << "}";
        }
        oss << "]";
        return oss.str();
    }

    void ExpectTimelineEquals(const std::vector<std::vector<RID>>& actualTimeline,
                              const std::vector<std::vector<RID>>& expectedTimeline) {
        ASSERT_EQ(actualTimeline.size(), expectedTimeline.size())
            << "Timeline layer count mismatch!\n"
            << "  Actual:   " << FormatTimeline(actualTimeline) << "\n"
            << "  Expected: " << FormatTimeline(expectedTimeline);
        for (size_t layer = 0; layer < expectedTimeline.size(); ++layer) {
            EXPECT_TRUE(SamePassSet(actualTimeline[layer], expectedTimeline[layer]))
                << "Mismatch at timeline layer " << layer << "!\n"
                << "  Actual:   " << FormatTimeline(actualTimeline) << "\n"
                << "  Expected: " << FormatTimeline(expectedTimeline);
        }
    }
} // namespace

// 1. 单个 Pass 基础测试
TEST(RenderGraphTimeline, SinglePass) {
    Ccino::RGBuilder builder;
    RID resA = CreateDummyBufferResource(builder);

    RID p0;
    builder.AddPass<"P0">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p0 = passRID;
        rg.Write<uint32_t>(resA, passRID);
    });

    builder.BuildGraph();

    const auto& timeline = GetTimeline(builder);
    ExpectTimelineEquals(timeline, {
        {p0}
    });
}

// 2. 完全独立的并行 Pass：互不依赖，应当全部调度在第 0 层
TEST(RenderGraphTimeline, ParallelIndependentPasses) {
    Ccino::RGBuilder builder;
    RID resA = CreateDummyBufferResource(builder);
    RID resB = CreateDummyBufferResource(builder);
    RID resC = CreateDummyBufferResource(builder);

    RID p0, p1, p2;
    builder.AddPass<"P0">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p0 = passRID;
        rg.Write<uint32_t>(resA, passRID);
    }).AddPass<"P1">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p1 = passRID;
        rg.Write<uint32_t>(resB, passRID);
    }).AddPass<"P2">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p2 = passRID;
        rg.Write<uint32_t>(resC, passRID);
    });

    builder.BuildGraph();

    const auto& timeline = GetTimeline(builder);
    ExpectTimelineEquals(timeline, {
        {p0, p1, p2}
    });
}

// 3. 线性 RAW (Read-After-Write) 依赖链：P0(Write A) -> P1(Read A, Write B) -> P2(Read B)
TEST(RenderGraphTimeline, LinearRAWChain) {
    Ccino::RGBuilder builder;
    RID resA = CreateDummyBufferResource(builder);
    RID resB = CreateDummyBufferResource(builder);

    RID p0, p1, p2;
    builder.AddPass<"P0">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p0 = passRID;
        rg.Write<uint32_t>(resA, passRID);
    }).AddPass<"P1">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p1 = passRID;
        rg.Read<uint32_t>(resA, passRID);
        rg.Write<uint32_t>(resB, passRID);
    }).AddPass<"P2">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p2 = passRID;
        rg.Read<uint32_t>(resB, passRID);
    });

    builder.BuildGraph();

    const auto& timeline = GetTimeline(builder);
    ExpectTimelineEquals(timeline, {
        {p0},
        {p1},
        {p2}
    });
}

// 4. 一写多读扇出 (Fan-Out / Multiple Readers)：多个 Read 不改变版本，应当在同一层并行执行
TEST(RenderGraphTimeline, ForkMultipleReaders) {
    Ccino::RGBuilder builder;
    RID resA = CreateDummyBufferResource(builder);

    RID p0, p1, p2, p3;
    builder.AddPass<"P0">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p0 = passRID;
        rg.Write<uint32_t>(resA, passRID);
    }).AddPass<"P1">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p1 = passRID;
        rg.Read<uint32_t>(resA, passRID);
    }).AddPass<"P2">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p2 = passRID;
        rg.Read<uint32_t>(resA, passRID);
    }).AddPass<"P3">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p3 = passRID;
        rg.Read<uint32_t>(resA, passRID);
    });

    builder.BuildGraph();

    const auto& timeline = GetTimeline(builder);
    ExpectTimelineEquals(timeline, {
        {p0},
        {p1, p2, p3}
    });
}

// 5. 多写一读扇入 (Fan-In / Join)：P0 写 A，P1 写 B，P2 同时读 A 和 B
TEST(RenderGraphTimeline, JoinMultipleProducers) {
    Ccino::RGBuilder builder;
    RID resA = CreateDummyBufferResource(builder);
    RID resB = CreateDummyBufferResource(builder);

    RID p0, p1, p2;
    builder.AddPass<"P0">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p0 = passRID;
        rg.Write<uint32_t>(resA, passRID);
    }).AddPass<"P1">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p1 = passRID;
        rg.Write<uint32_t>(resB, passRID);
    }).AddPass<"P2">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p2 = passRID;
        rg.Read<uint32_t>(resA, passRID);
        rg.Read<uint32_t>(resB, passRID);
    });

    builder.BuildGraph();

    const auto& timeline = GetTimeline(builder);
    ExpectTimelineEquals(timeline, {
        {p0, p1},
        {p2}
    });
}

// 6. WAR (Write-After-Read) 依赖：P0(Write A) -> {P1(Read A), P2(Read A)} -> P3(Write A) -> P4(Read A)
TEST(RenderGraphTimeline, WriteAfterReadDependency) {
    Ccino::RGBuilder builder;
    RID resA = CreateDummyBufferResource(builder);

    RID p0, p1, p2, p3, p4;
    builder.AddPass<"P0">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p0 = passRID;
        rg.Write<uint32_t>(resA, passRID);
    }).AddPass<"P1">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p1 = passRID;
        rg.Read<uint32_t>(resA, passRID);
    }).AddPass<"P2">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p2 = passRID;
        rg.Read<uint32_t>(resA, passRID);
    }).AddPass<"P3">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p3 = passRID;
        rg.Write<uint32_t>(resA, passRID);
    }).AddPass<"P4">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p4 = passRID;
        rg.Read<uint32_t>(resA, passRID);
    });

    builder.BuildGraph();

    const auto& timeline = GetTimeline(builder);
    ExpectTimelineEquals(timeline, {
        {p0},
        {p1, p2},
        {p3},
        {p4}
    });
}

// 7. 初始资源先读后写再读：P0(Read A) -> P1(Write A) -> P2(Read A)
TEST(RenderGraphTimeline, InitialReadThenWrite) {
    Ccino::RGBuilder builder;
    RID resA = CreateDummyBufferResource(builder);

    RID p0, p1, p2;
    builder.AddPass<"P0">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p0 = passRID;
        rg.Read<uint32_t>(resA, passRID);
    }).AddPass<"P1">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p1 = passRID;
        rg.Write<uint32_t>(resA, passRID);
    }).AddPass<"P2">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p2 = passRID;
        rg.Read<uint32_t>(resA, passRID);
    });

    builder.BuildGraph();

    const auto& timeline = GetTimeline(builder);
    ExpectTimelineEquals(timeline, {
        {p0},
        {p1},
        {p2}
    });
}

// 8. WAW (Write-After-Write / WRW) 依赖：P0(Write A) -> P1(Write A) -> P2(Write A)
TEST(RenderGraphTimeline, WriteAfterWriteDependency) {
    Ccino::RGBuilder builder;
    RID resA = CreateDummyBufferResource(builder);

    RID p0, p1, p2;
    builder.AddPass<"P0">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p0 = passRID;
        rg.Write<uint32_t>(resA, passRID);
    }).AddPass<"P1">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p1 = passRID;
        rg.Write<uint32_t>(resA, passRID);
    }).AddPass<"P2">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p2 = passRID;
        rg.Write<uint32_t>(resA, passRID);
    });

    builder.BuildGraph();

    const auto& timeline = GetTimeline(builder);
    ExpectTimelineEquals(timeline, {
        {p0},
        {p1},
        {p2}
    });
}

// 9. ReadWrite 链：P0(Write A) -> P1(ReadWrite A) -> P2(Read A)
TEST(RenderGraphTimeline, ReadWriteChain) {
    Ccino::RGBuilder builder;
    RID resA = CreateDummyBufferResource(builder);

    RID p0, p1, p2;
    builder.AddPass<"P0">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p0 = passRID;
        rg.Write<uint32_t>(resA, passRID);
    }).AddPass<"P1">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p1 = passRID;
        rg.ReadWrite<uint32_t>(resA, passRID);
    }).AddPass<"P2">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p2 = passRID;
        rg.Read<uint32_t>(resA, passRID);
    });

    builder.BuildGraph();

    const auto& timeline = GetTimeline(builder);
    ExpectTimelineEquals(timeline, {
        {p0},
        {p1},
        {p2}
    });
}

// 10. 菱形 DAG (Diamond Graph)：
//     P0(Write A)
//     ├── P1(Read A, Write B)
//     └── P2(Read A, Write C)
//     P3(Read B, Read C, Write D)
//     P4(Read D)
TEST(RenderGraphTimeline, DiamondDAG) {
    Ccino::RGBuilder builder;
    RID resA = CreateDummyBufferResource(builder);
    RID resB = CreateDummyBufferResource(builder);
    RID resC = CreateDummyBufferResource(builder);
    RID resD = CreateDummyBufferResource(builder);

    RID p0, p1, p2, p3, p4;
    builder.AddPass<"P0">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p0 = passRID;
        rg.Write<uint32_t>(resA, passRID);
    }).AddPass<"P1">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p1 = passRID;
        rg.Read<uint32_t>(resA, passRID);
        rg.Write<uint32_t>(resB, passRID);
    }).AddPass<"P2">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p2 = passRID;
        rg.Read<uint32_t>(resA, passRID);
        rg.Write<uint32_t>(resC, passRID);
    }).AddPass<"P3">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p3 = passRID;
        rg.Read<uint32_t>(resB, passRID);
        rg.Read<uint32_t>(resC, passRID);
        rg.Write<uint32_t>(resD, passRID);
    }).AddPass<"P4">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p4 = passRID;
        rg.Read<uint32_t>(resD, passRID);
    });

    builder.BuildGraph();

    const auto& timeline = GetTimeline(builder);
    ExpectTimelineEquals(timeline, {
        {p0},
        {p1, p2},
        {p3},
        {p4}
    });
}

// 11. RenderTarget 作为写入：P0(RT A) -> P1(Sample A) -> P2(RT A) -> P3(Sample A)
TEST(RenderGraphTimeline, RenderTargetWriteChain) {
    Ccino::RGBuilder builder;
    RID texA = builder.CreateTransientTexture2D(Ccino::Texture2D::CreateArgs{});

    RID p0, p1, p2, p3;
    builder.AddPass<"RTChainDraw0">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p0 = passRID;
        auto rtA = rg.RTView<Ccino::ViewType::Float>(texA, passRID);
        rg.AddVSAndFS([]() {}, []() {}, Ccino::GraphicsSetting{}, std::vector{rtA});
    }).AddPass<"RTChainSample0">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p1 = passRID;
        rg.Sample<Ccino::ViewType::Float>(texA, passRID);
    }).AddPass<"RTChainDraw1">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p2 = passRID;
        auto rtA = rg.RTView<Ccino::ViewType::Float>(texA, passRID);
        rg.AddVSAndFS([]() {}, []() {}, Ccino::GraphicsSetting{}, std::vector{rtA});
    }).AddPass<"RTChainSample1">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p3 = passRID;
        rg.Sample<Ccino::ViewType::Float>(texA, passRID);
    });

    builder.BuildGraph();

    const auto& timeline = GetTimeline(builder);
    ExpectTimelineEquals(timeline, {
        {p0},
        {p1},
        {p2},
        {p3}
    });
}

// 12. 两个图形 Pass 写不同 RenderTarget 可并行，P2 同时采样两者
TEST(RenderGraphTimeline, IndependentRenderTargetsJoin) {
    Ccino::RGBuilder builder;
    RID texA = builder.CreateTransientTexture2D(Ccino::Texture2D::CreateArgs{});
    RID texB = builder.CreateTransientTexture2D(Ccino::Texture2D::CreateArgs{});

    RID p0, p1, p2;
    builder.AddPass<"RTJoinDrawA">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p0 = passRID;
        auto rtA = rg.RTView<Ccino::ViewType::Float>(texA, passRID);
        rg.AddVSAndFS([]() {}, []() {}, Ccino::GraphicsSetting{}, std::vector{rtA});
    }).AddPass<"RTJoinDrawB">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p1 = passRID;
        auto rtB = rg.RTView<Ccino::ViewType::Float>(texB, passRID);
        rg.AddVSAndFS([]() {}, []() {}, Ccino::GraphicsSetting{}, std::vector{rtB});
    }).AddPass<"RTJoinSample">([&](Ccino::RGBuilder& rg, RID& passRID) {
        p2 = passRID;
        rg.Sample<Ccino::ViewType::Float>(texA, passRID);
        rg.Sample<Ccino::ViewType::Float>(texB, passRID);
    });

    builder.BuildGraph();

    const auto& timeline = GetTimeline(builder);
    ExpectTimelineEquals(timeline, {
        {p0, p1},
        {p2}
    });
}
