// RayQuery AS + PS 可视化 Demo:
//   1. 在 CPU 构造 Cube 与 Ground 两个 BLAS, 并组合成包含 3 个实例的 TLAS
//   2. 每帧通过一个全屏 Graphics Pass (VS 生成全屏三角形 + FS/PS 发射主光线与阴影光线) 直接可视化 TLAS
// 用法: RayQueryDemo [帧数], 不传或传 0 时一直运行到关闭窗口或按 Esc
#include "Context/CcinoContext.h"
#include "Core/AccelerationStructure.h"
#include "Core/Buffer.h"
#include "Core/CcinoType.h"
#include "Core/Texture2D.h"
#include "RG/RenderGraph.h"

#include <SDL3/SDL_events.h>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <vector>

namespace {
    constexpr uint32_t kWidth = 960;
    constexpr uint32_t kHeight = 600;
    constexpr Ccino::ClearColor kClearColor{{0.05f, 0.06f, 0.08f, 1.0f}};

    struct PositionVertex {
        float x, y, z;
    };

    int RunDemo(uint64_t maxFrames) {
        Ccino::Context ctx;
        RHISettings settings{};
        settings.bUseSwapChain = true;
        settings.flightFrameCount = 2;
        settings.windowWidth = kWidth;
        settings.windowHeight = kHeight;
        settings.bVsync = true;
        if (!ctx.Init(settings)) {
            std::cerr << "Failed to initialize Ccino context\n";
            return 1;
        }

        // 1. 构建单位立方体 BLAS
        const std::vector<PositionVertex> cubeVertices = {
            {-0.5f, -0.5f,  0.5f},
            { 0.5f, -0.5f,  0.5f},
            { 0.5f,  0.5f,  0.5f},
            {-0.5f,  0.5f,  0.5f},
            {-0.5f, -0.5f, -0.5f},
            { 0.5f, -0.5f, -0.5f},
            { 0.5f,  0.5f, -0.5f},
            {-0.5f,  0.5f, -0.5f},
        };
        const std::vector<uint32_t> cubeIndices = {
            0, 1, 2,  0, 2, 3, // +Z
            5, 4, 7,  5, 7, 6, // -Z
            4, 0, 3,  4, 3, 7, // -X
            1, 5, 6,  1, 6, 2, // +X
            3, 2, 6,  3, 6, 7, // +Y
            4, 5, 1,  4, 1, 0, // -Y
        };

        auto cubeVB = ctx.CreateBuffer({.size = static_cast<uint32_t>(sizeof(PositionVertex) * cubeVertices.size())});
        auto cubeIB = ctx.CreateBuffer({.size = static_cast<uint32_t>(sizeof(uint32_t) * cubeIndices.size())});
        cubeVB->Copy(cubeVertices.data());
        cubeIB->Copy(cubeIndices.data());
        cubeVB->Flush();
        cubeIB->Flush();

        auto cubeBLAS = ctx.CreateBLAS({
            .vertexBuffer  = cubeVB,
            .vertexStride  = sizeof(PositionVertex),
            .vertexCount   = static_cast<uint32_t>(cubeVertices.size()),
            .indexType     = Ccino::RHI::IndexType::e32,
            .indexBuffer   = cubeIB,
            .indexCount    = static_cast<uint32_t>(cubeIndices.size()),
            .geometryFlags = Ccino::RHI::GeometryFlagBits::QPAQUE,
        });

        // 2. 构建地面平面 BLAS
        const std::vector<PositionVertex> planeVertices = {
            {-4.0f, 0.0f, -4.0f},
            { 4.0f, 0.0f, -4.0f},
            { 4.0f, 0.0f,  4.0f},
            {-4.0f, 0.0f,  4.0f},
        };
        const std::vector<uint32_t> planeIndices = {0, 2, 1, 0, 3, 2};

        auto planeVB = ctx.CreateBuffer({.size = static_cast<uint32_t>(sizeof(PositionVertex) * planeVertices.size())});
        auto planeIB = ctx.CreateBuffer({.size = static_cast<uint32_t>(sizeof(uint32_t) * planeIndices.size())});
        planeVB->Copy(planeVertices.data());
        planeIB->Copy(planeIndices.data());
        planeVB->Flush();
        planeIB->Flush();

        auto planeBLAS = ctx.CreateBLAS({
            .vertexBuffer  = planeVB,
            .vertexStride  = sizeof(PositionVertex),
            .vertexCount   = static_cast<uint32_t>(planeVertices.size()),
            .indexType     = Ccino::RHI::IndexType::e32,
            .indexBuffer   = planeIB,
            .indexCount    = static_cast<uint32_t>(planeIndices.size()),
            .geometryFlags = Ccino::RHI::GeometryFlagBits::QPAQUE,
        });

        // 3. 组合成 TLAS (地面 + 两个立方体实例)
        auto tlas = ctx.CreateTLAS({
            .instances = {
                {.position = { 0.0f, -0.5f,  0.0f}, .blas = planeBLAS, .customIndex = 0, .bCulling = false},
                {.position = {-0.8f,  0.0f,  0.1f}, .blas = cubeBLAS,  .customIndex = 1, .bCulling = false},
                {.position = { 0.85f, 0.35f,-0.3f}, .blas = cubeBLAS,  .customIndex = 2, .bCulling = false},
            }
        });

        // 4. 离屏渲染目标 (sRGB, 最后 Blit 到交换链)
        uint32_t curWidth = kWidth;
        uint32_t curHeight = kHeight;
        auto createTarget = [&](uint32_t w, uint32_t h) {
            return ctx.CreateTexture2D(Ccino::Texture2D::CreateArgs{
                .width = w,
                .height = h,
                .mipLevels = 1,
                .format = Ccino::TextureFormat::RGBA8_sRGB,
                .usage = Ccino::TextureUsage::ColorAttachment | Ccino::TextureUsage::TransferSrc,
            });
        };
        Ccino::Ref<Ccino::Texture2D> target = createTarget(curWidth, curHeight);

        Ccino::RGBuilder* builder = ctx.CreateRGBuilder("RayQueryDemo");
        RID targetRID = builder->CreateExternalTexture2D(target);
        RID tlasRID   = builder->CreateExternalTLAS(tlas);

        const auto start = std::chrono::steady_clock::now();

        // 5. 全屏 Graphics Pass: VS 输出全屏三角形, FS (AS + PS) 做主光线 + 阴影光线追踪
        builder->AddPass<"RayQueryASPSPass">([&](Ccino::RGBuilder& rg, RID& passRID) {
            auto sceneAS = rg.ReadAS(tlasRID, passRID);

            const float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
            const float orbitAngle = t * 0.6f;
            const float radius = 3.6f;
            const Ccino::Vec3 camPos(std::sin(orbitAngle) * radius, 1.35f, std::cos(orbitAngle) * radius);
            const Ccino::Vec3 targetPos(0.0f, -0.05f, 0.0f);
            const Ccino::Vec3 camForward = glm::normalize(targetPos - camPos);
            const Ccino::Vec3 worldUp(0.0f, 1.0f, 0.0f);
            const Ccino::Vec3 camRight = glm::normalize(glm::cross(camForward, worldUp));
            const Ccino::Vec3 camUp = glm::normalize(glm::cross(camRight, camForward));
            const float aspect = curHeight > 0 ? static_cast<float>(curWidth) / static_cast<float>(curHeight) : 1.0f;
            const Ccino::Vec3 lightDir = glm::normalize(Ccino::Vec3(0.65f, 1.0f, 0.45f));

            auto vs = [=](Ccino::Vec2& uv) {
                // 全屏三角形 (VertexIndex = 0, 1, 2)
                Ccino::Vec2 pos(-1.0f, -1.0f);
                uv = Ccino::Vec2(0.0f, 0.0f);
                if (Ccino::VertexIndex == 1) {
                    pos = Ccino::Vec2(3.0f, -1.0f);
                    uv = Ccino::Vec2(2.0f, 0.0f);
                }
                if (Ccino::VertexIndex == 2) {
                    pos = Ccino::Vec2(-1.0f, 3.0f);
                    uv = Ccino::Vec2(0.0f, 2.0f);
                }
                Ccino::Position = Ccino::Vec4(pos, 0.0f, 1.0f);
            };

            auto fs = [=](const Ccino::Vec2& uv, Ccino::Vec4& outColor) {
                // Vulkan 屏幕 y 向下, 将 v 翻转成世界空间向上
                Ccino::Vec2 ndc(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
                float tanHalfFov = 0.55f;
                Ccino::Vec3 rayDir = Ccino::Normalize(
                    camForward +
                    camRight * (ndc.x * aspect * tanHalfFov) +
                    camUp * (ndc.y * tanHalfFov));

                Ccino::RayQueryEXT rq;
                Ccino::RayQueryInitializeEXT(
                    rq, sceneAS,
                    Ccino::RayFlagsOpaqueEXT, 0xFFu,
                    camPos, 0.001f, rayDir, 100.0f);
                while (Ccino::RayQueryProceedEXT(rq)) {
                }

                // 默认天空渐变
                float skyT = Ccino::Clamp(rayDir.y * 0.5f + 0.5f, 0.0f, 1.0f);
                Ccino::Vec3 color = Ccino::Mix(Ccino::Vec3(0.08f, 0.10f, 0.14f), Ccino::Vec3(0.24f, 0.42f, 0.72f), skyT);

                if (Ccino::RayQueryGetIntersectionTypeEXT(rq, true) == Ccino::RayQueryCommittedIntersectionTriangleEXT) {
                    float hitT = Ccino::RayQueryGetIntersectionTEXT(rq, true);
                    Ccino::Vec2 bary = Ccino::RayQueryGetIntersectionBarycentricsEXT(rq, true);
                    int32_t instId = Ccino::RayQueryGetIntersectionInstanceCustomIndexEXT(rq, true);
                    Ccino::Vec3 hitPos = camPos + rayDir * hitT;

                    Ccino::Vec3 baseColor(0.8f, 0.8f, 0.8f);
                    if (instId == 0) {
                        // 地面棋盘格
                        float cx = Ccino::Floor(hitPos.x * 2.0f);
                        float cz = Ccino::Floor(hitPos.z * 2.0f);
                        float checker = Ccino::Mod(cx + cz, 2.0f);
                        baseColor = checker < 0.5f ? Ccino::Vec3(0.26f, 0.28f, 0.32f) : Ccino::Vec3(0.45f, 0.48f, 0.54f);
                    } else if (instId == 1) {
                        baseColor = Ccino::Vec3(1.0f - bary.x - bary.y, bary.x, bary.y) * 0.65f + Ccino::Vec3(0.28f, 0.15f, 0.10f);
                    } else {
                        baseColor = Ccino::Vec3(bary.x, 1.0f - bary.x - bary.y, bary.y) * 0.65f + Ccino::Vec3(0.10f, 0.25f, 0.35f);
                    }

                    // 阴影光线检测
                    Ccino::RayQueryEXT shadowRq;
                    Ccino::RayQueryInitializeEXT(
                        shadowRq, sceneAS,
                        Ccino::RayFlagsOpaqueEXT | Ccino::RayFlagsTerminateOnFirstHitEXT,
                        0xFFu,
                        hitPos - rayDir * 0.002f, 0.001f, lightDir, 50.0f);
                    while (Ccino::RayQueryProceedEXT(shadowRq)) {
                    }

                    bool inShadow = (Ccino::RayQueryGetIntersectionTypeEXT(shadowRq, true) != Ccino::RayQueryCommittedIntersectionNoneEXT);
                    float lightMul = inShadow ? 0.25f : 1.0f;
                    color = baseColor * lightMul;
                }

                outColor = Ccino::Vec4(color, 1.0f);
            };

            auto renderTarget = rg.RTView(targetRID, passRID, Ccino::LoadOp::Clear, kClearColor);
            rg.AddVSAndFS(vs, fs, Ccino::GraphicsSetting{.drawCounts = 3}, {renderTarget});
        });

        uint64_t frameCount = 0;
        builder->LoopRun(targetRID, Ccino::LoopHooks{
            .onEvent = [](SDL_Event& event) {
                return event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE;
            },
            .onResize = [&](uint32_t w, uint32_t h) {
                if (w > 0 && h > 0 && (w != curWidth || h != curHeight)) {
                    curWidth = w;
                    curHeight = h;
                    target = createTarget(curWidth, curHeight);
                    builder->UpdateExternalTexture2D(targetRID, target);
                }
            },
            .onUpdate = [&](Ccino::FrameContext& frame) {
                if (maxFrames != 0 && frame.frameIndex >= maxFrames) {
                    frame.shouldExit = true;
                }
            },
            .onPostFrame = [&](Ccino::FrameContext& frame) {
                frameCount = frame.frameIndex;
            },
        });

        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::cout << "Rendered " << frameCount << " frames in " << seconds << " s";
        if (seconds > 0.0) {
            std::cout << " (" << frameCount / seconds << " FPS)";
        }
        std::cout << std::endl;

        target.Reset();
        tlas.Reset();
        cubeBLAS.Reset();
        planeBLAS.Reset();
        cubeVB.Reset();
        cubeIB.Reset();
        planeVB.Reset();
        planeIB.Reset();
        ctx.DestroyRGBuilder("RayQueryDemo");
        ctx.Destroy();
        return 0;
    }
}

int main(int argc, char** argv) {
    const uint64_t maxFrames = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 0;
    try {
        return RunDemo(maxFrames);
    } catch (const std::exception& e) {
        std::cerr << "RayQueryDemo failed: " << e.what() << "\n";
        return 1;
    }
}
