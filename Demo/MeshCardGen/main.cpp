// GPU MeshCard 生成与可视化 Demo (参考 UE MeshCardRepresentationUtilities)
// 流程: 球体 BLAS/TLAS -> RayQuery 体素化生成 Surfel -> 6 方向聚类成候选 MeshCard -> 按 WeightedCoverage 排序取 Top-K
// 操作: 左键/右键拖动旋转视角 | 滚轮缩放 | Esc 退出
// 用法: MeshCardGen [帧数], 不传或传 0 时持续运行
#include "MeshCard.h"

#include "Context/CcinoContext.h"
#include "Core/AccelerationStructure.h"
#include "Core/Buffer.h"
#include "Core/CcinoType.h"
#include "Core/Texture2D.h"
#include "RG/RenderGraph.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <vector>

namespace {
    constexpr uint32_t kWidth = 1280;
    constexpr uint32_t kHeight = 720;
    constexpr Ccino::ClearColor kBgColor{{0.08f, 0.09f, 0.12f, 1.0f}};
    constexpr const char* kDirNames[6] = {"-X", "+X", "-Y", "+Y", "-Z", "+Z"};

    // GPU Shader 辅助函数 (平铺实现, 不嵌套调用其他自定义函数)
    Ccino::Vec3 DirectionColor(uint32_t dir) {
        if (dir == 0u) return Ccino::Vec3(0.95f, 0.30f, 0.30f); // -X
        if (dir == 1u) return Ccino::Vec3(1.00f, 0.62f, 0.18f); // +X
        if (dir == 2u) return Ccino::Vec3(0.25f, 0.85f, 0.42f); // -Y
        if (dir == 3u) return Ccino::Vec3(0.86f, 0.90f, 0.22f); // +Y
        if (dir == 4u) return Ccino::Vec3(0.28f, 0.56f, 0.98f); // -Z
        return Ccino::Vec3(0.22f, 0.88f, 0.95f);                // +Z
    }

    struct OrbitCamera {
        float yaw = 0.65f;
        float pitch = 0.38f;
        float distance = 3.4f;
        bool dragging = false;

        void HandleEvent(const SDL_Event& event) {
            if (ImGui::GetIO().WantCaptureMouse) {
                if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                    dragging = false;
                }
                return;
            }
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT)) {
                dragging = true;
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
                       (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT)) {
                dragging = false;
            } else if (event.type == SDL_EVENT_MOUSE_MOTION && dragging) {
                yaw -= event.motion.xrel * 0.006f;
                pitch = std::clamp(pitch + event.motion.yrel * 0.006f, -1.45f, 1.45f);
            } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                distance = std::clamp(distance * std::pow(0.9f, event.wheel.y), 1.6f, 10.0f);
            }
        }

        Ccino::Vec3 Position() const {
            const float cp = std::cos(pitch);
            return Ccino::Vec3(std::sin(yaw) * cp, std::sin(pitch), std::cos(yaw) * cp) * distance;
        }

        Ccino::Mat4 ViewProj(float aspect) const {
            Ccino::Mat4 proj = glm::perspectiveRH_ZO(glm::radians(50.0f), aspect, 0.05f, 50.0f);
            proj[1][1] *= -1.0f; // Vulkan 裁剪空间 Y 向下
            Ccino::Mat4 view = glm::lookAt(Position(), Ccino::Vec3(0.0f), Ccino::Vec3(0.0f, 1.0f, 0.0f));
            return proj * view;
        }
    };

    struct ViewOptions {
        bool showSphere = true;
        bool showSurfels = true;
        bool showCardWire = true;
        bool showCardFill = true;
        bool autoRegenerate = true;
        int surfelColorMode = 0; // 0: Direction, 1: WeightedCoverage, 2: Top-K Card
        int dirFilter = -1;      // -1: All, 0..5: 指定方向
        int selectedCard = -1;   // -1: 无高亮, >=0: 高亮该 Top-K 卡片
        float surfelScale = 0.72f;
        float cardFillAlpha = 0.12f;
    };

    int RunDemo(uint64_t maxFrames) {
        Ccino::Context ctx;
        RHISettings rhiSettings{};
        rhiSettings.bUseSwapChain = true;
        rhiSettings.bUseImgui = true;
        rhiSettings.flightFrameCount = 2;
        rhiSettings.windowWidth = kWidth;
        rhiSettings.windowHeight = kHeight;
        rhiSettings.bVsync = true;
        if (!ctx.Init(rhiSettings)) {
            std::cerr << "Failed to initialize Ccino context\n";
            return 1;
        }

        // 1. 构建球体网格 + BLAS + TLAS
        std::vector<MeshCardGen::SphereVertex> sphereVerts;
        std::vector<uint32_t> sphereIndices;
        MeshCardGen::Box sphereBounds{};
        MeshCardGen::BuildSphereMesh(1.0f, 24, 32, sphereVerts, sphereIndices, sphereBounds);

        auto sphereVB = ctx.CreateBuffer({.size = static_cast<uint32_t>(sphereVerts.size() * sizeof(MeshCardGen::SphereVertex))});
        auto sphereIB = ctx.CreateBuffer({.size = static_cast<uint32_t>(sphereIndices.size() * sizeof(uint32_t))});
        sphereVB->Copy(sphereVerts.data());
        sphereIB->Copy(sphereIndices.data());
        sphereVB->Flush();
        sphereIB->Flush();

        auto sphereBLAS = ctx.CreateBLAS({
            .vertexBuffer  = sphereVB,
            .vertexStride  = sizeof(MeshCardGen::SphereVertex),
            .vertexCount   = static_cast<uint32_t>(sphereVerts.size()),
            .indexType     = Ccino::RHI::IndexType::e32,
            .indexBuffer   = sphereIB,
            .indexCount    = static_cast<uint32_t>(sphereIndices.size()),
            .geometryFlags = Ccino::RHI::GeometryFlagBits::QPAQUE,
        });

        auto sphereTLAS = ctx.CreateTLAS({
            .instances = {
                {.position = {0.0f, 0.0f, 0.0f}, .blas = sphereBLAS, .customIndex = 0, .bCulling = false},
            }
        });

        // 2. 初始化 GPU MeshCard 生成器并执行首次生成
        MeshCardGen::MeshCardGenerator generator;
        generator.Init(ctx, sphereVB, sphereIB, sphereTLAS, sphereBounds);

        MeshCardGen::GenSettings genSettings{};
        ViewOptions viewOpts{};
        OrbitCamera camera{};
        uint32_t pendingRepeat = 0;

        generator.Generate(ctx, genSettings, 1);
        {
            const auto& initStats = generator.GetStats();
            std::cout << "Initial GPU MeshCardGen: surfels=" << initStats.totalSurfels
                      << " candidates=" << initStats.candidateCards
                      << " topK=" << initStats.topKCards
                      << " time=" << initStats.lastGpuMs << " ms" << std::endl;
        }

        // 3. 创建显示用的颜色与深度 RenderTarget
        uint32_t curWidth = kWidth;
        uint32_t curHeight = kHeight;
        auto createColorRT = [&](uint32_t w, uint32_t h) {
            return ctx.CreateTexture2D({
                .width = w,
                .height = h,
                .mipLevels = 1,
                .format = Ccino::TextureFormat::RGBA8_sRGB,
                .usage = Ccino::TextureUsage::ColorAttachment | Ccino::TextureUsage::TransferSrc,
            });
        };
        auto createDepthRT = [&](uint32_t w, uint32_t h) {
            return ctx.CreateTexture2D({
                .width = w,
                .height = h,
                .mipLevels = 1,
                .format = Ccino::TextureFormat::D32_Float,
                .usage = Ccino::TextureUsage::DepthAttachment,
            });
        };

        auto colorRT = createColorRT(curWidth, curHeight);
        auto depthRT = createDepthRT(curWidth, curHeight);

        Ccino::RGBuilder* builder = ctx.CreateRGBuilder("MeshCardVisualizer");
        RID colorRID   = builder->CreateExternalTexture2D(colorRT);
        RID depthRID   = builder->CreateExternalTexture2D(depthRT);
        RID vbRID      = builder->CreateExternalBuffer(sphereVB);
        RID ibRID      = builder->CreateExternalBuffer(sphereIB);
        RID surfelsRID = builder->CreateExternalBuffer(generator.GetSurfelsBuffer());
        RID cardsRID   = builder->CreateExternalBuffer(generator.GetCardsBuffer());

        const uint32_t sphereIndexCount = static_cast<uint32_t>(sphereIndices.size());

        // Pass A: 清屏并绘制球体网格
        builder->AddPass<"MeshCardSpherePass">([&](Ccino::RGBuilder& rg, RID& pass) {
            const float aspect = curHeight > 0 ? static_cast<float>(curWidth) / static_cast<float>(curHeight) : 1.0f;
            Ccino::Mat4 vp = camera.ViewProj(aspect);
            auto vb = rg.Read<MeshCardGen::SphereVertex>(vbRID, pass);
            auto ib = rg.Read<MeshCardGen::IndexElem>(ibRID, pass);

            auto vs = [=](Ccino::Vec3& normal) {
                uint32_t idx = ib[Ccino::VertexIndex].value;
                normal = vb[idx].normal;
                Ccino::Position = vp * Ccino::Vec4(vb[idx].pos, 1.0f);
            };
            auto fs = [=](const Ccino::Vec3& normal, Ccino::Vec4& outColor) {
                Ccino::Vec3 n = Ccino::Normalize(normal);
                Ccino::Vec3 lightDir = Ccino::Normalize(Ccino::Vec3(0.6f, 0.85f, 0.5f));
                float diff = Ccino::Max(Ccino::Dot(n, lightDir), 0.0f);
                float hemi = n.y * 0.5f + 0.5f;
                Ccino::Vec3 col = Ccino::Mix(Ccino::Vec3(0.18f, 0.20f, 0.24f), Ccino::Vec3(0.32f, 0.36f, 0.42f), hemi)
                                + Ccino::Vec3(0.45f, 0.48f, 0.52f) * diff;
                outColor = Ccino::Vec4(col, 1.0f);
            };

            auto rt = rg.RTView(colorRID, pass, Ccino::LoadOp::Clear, kBgColor);
            auto dt = rg.RTView(depthRID, pass, Ccino::LoadOp::Clear, Ccino::ClearDepthStencil{1.0f, 0});
            rg.AddVSAndFS(vs, fs, Ccino::GraphicsSetting{
                .drawCounts = viewOpts.showSphere ? sphereIndexCount : 0u,
                .cullMode = Ccino::CullMode::Back,
                .depthTestEnable = true,
                .depthWriteEnable = true,
            }, {rt}, &dt);
        });

        // Pass B: 可视化体素化生成的 Surfel (每个 Surfel 展开为一个朝向对应轴的小方片)
        builder->AddPass<"MeshCardSurfelPass">([&](Ccino::RGBuilder& rg, RID& pass) {
            const uint32_t totalSurfels = generator.GetStats().totalSurfels;
            if (!viewOpts.showSurfels || totalSurfels == 0) {
                return;
            }
            const float aspect = curHeight > 0 ? static_cast<float>(curWidth) / static_cast<float>(curHeight) : 1.0f;
            Ccino::Mat4 vp = camera.ViewProj(aspect);
            auto surfels = rg.Read<MeshCardGen::SurfelGPU>(surfelsRID, pass);
            float halfSize = generator.GetVoxelSize() * 0.45f * viewOpts.surfelScale;
            int32_t colorMode = viewOpts.surfelColorMode;
            int32_t dirFilter = viewOpts.dirFilter;
            int32_t selectedCard = viewOpts.selectedCard;
            uint32_t invalidId = MeshCardGen::kInvalidClusterId;

            auto vs = [=](Ccino::Vec3& col) {
                uint32_t surfelIdx = uint32_t(Ccino::VertexIndex) / 6u;
                uint32_t cornerIdx = uint32_t(Ccino::VertexIndex) % 6u;

                uint32_t sValid = surfels[surfelIdx].valid;
                uint32_t sDir = surfels[surfelIdx].dirIndex;
                uint32_t sCluster = surfels[surfelIdx].clusterId;

                bool visible = (sValid != 0u);
                if (dirFilter >= 0 && int32_t(sDir) != dirFilter) {
                    visible = false;
                }
                if (selectedCard >= 0 && int32_t(sCluster) != selectedCard) {
                    visible = false;
                }

                if (!visible) {
                    col = Ccino::Vec3(0.0f);
                    Ccino::Position = Ccino::Vec4(2.0f, 2.0f, 2.0f, 1.0f);
                    return;
                }

                float u = (cornerIdx == 1u || cornerIdx == 2u || cornerIdx == 4u) ? 1.0f : -1.0f;
                float v = (cornerIdx == 2u || cornerIdx == 4u || cornerIdx == 5u) ? 1.0f : -1.0f;

                Ccino::Vec3 ax(1.0f, 0.0f, 0.0f);
                Ccino::Vec3 ay(0.0f, 1.0f, 0.0f);
                uint32_t axis = sDir / 2u;
                if (axis == 0u) {
                    ax = Ccino::Vec3(0.0f, 1.0f, 0.0f);
                    ay = Ccino::Vec3(0.0f, 0.0f, 1.0f);
                } else if (axis == 1u) {
                    ax = Ccino::Vec3(1.0f, 0.0f, 0.0f);
                    ay = Ccino::Vec3(0.0f, 0.0f, 1.0f);
                }

                Ccino::Vec3 worldPos = surfels[surfelIdx].worldPos + surfels[surfelIdx].worldNormal * 0.008f + (ax * u + ay * v) * halfSize;

                if (colorMode == 0) {
                    col = DirectionColor(sDir) * (0.45f + 0.55f * surfels[surfelIdx].coverage);
                } else if (colorMode == 1) {
                    float t = Ccino::Clamp(surfels[surfelIdx].weightedCoverage * 0.5f, 0.0f, 1.0f);
                    col = Ccino::Mix(Ccino::Vec3(0.10f, 0.25f, 0.85f), Ccino::Vec3(1.0f, 0.90f, 0.15f), t);
                } else {
                    if (sCluster == invalidId) {
                        col = Ccino::Vec3(0.22f, 0.22f, 0.25f);
                    } else {
                        col = DirectionColor(sCluster % 6u);
                    }
                }

                Ccino::Position = vp * Ccino::Vec4(worldPos, 1.0f);
            };

            auto fs = [=](const Ccino::Vec3& col, Ccino::Vec4& outColor) {
                outColor = Ccino::Vec4(col, 1.0f);
            };

            auto rt = rg.RTView(colorRID, pass, Ccino::LoadOp::Load);
            auto dt = rg.RTView(depthRID, pass, Ccino::LoadOp::Load);
            rg.AddVSAndFS(vs, fs, Ccino::GraphicsSetting{
                .drawCounts = totalSurfels * 6u,
                .cullMode = Ccino::CullMode::None,
                .depthTestEnable = true,
                .depthWriteEnable = true,
            }, {rt}, &dt);
        });

        // Pass C: Top-K MeshCard 半透明 OBB 填充
        builder->AddPass<"MeshCardBoxPass">([&](Ccino::RGBuilder& rg, RID& pass) {
            const uint32_t topK = generator.GetStats().topKCards;
            if (!viewOpts.showCardFill || topK == 0 || viewOpts.cardFillAlpha <= 0.001f) {
                return;
            }
            const float aspect = curHeight > 0 ? static_cast<float>(curWidth) / static_cast<float>(curHeight) : 1.0f;
            Ccino::Mat4 vp = camera.ViewProj(aspect);
            auto cards = rg.Read<MeshCardGen::MeshCardGPU>(cardsRID, pass);
            int32_t dirFilter = viewOpts.dirFilter;
            int32_t selectedCard = viewOpts.selectedCard;
            float baseAlpha = viewOpts.cardFillAlpha;

            auto vs = [=](Ccino::Vec4& col) {
                uint32_t cardIdx = uint32_t(Ccino::VertexIndex) / 36u;
                uint32_t localV  = uint32_t(Ccino::VertexIndex) % 36u;
                uint32_t face    = localV / 6u;
                uint32_t tv      = localV % 6u;

                uint32_t cActive = cards[cardIdx].isActive;
                uint32_t cDir    = cards[cardIdx].dirIndex;
                bool visible = (cActive != 0u);
                if (dirFilter >= 0 && int32_t(cDir) != dirFilter) {
                    visible = false;
                }
                if (selectedCard >= 0 && int32_t(cardIdx) != selectedCard) {
                    visible = false;
                }
                if (!visible) {
                    col = Ccino::Vec4(0.0f);
                    Ccino::Position = Ccino::Vec4(2.0f, 2.0f, 2.0f, 1.0f);
                    return;
                }

                float u = (tv == 1u || tv == 2u || tv == 4u) ? 1.0f : -1.0f;
                float v = (tv == 2u || tv == 4u || tv == 5u) ? 1.0f : -1.0f;
                float sgn = ((face & 1u) != 0u) ? 1.0f : -1.0f;
                uint32_t axis = face / 2u;

                Ccino::Vec3 lp(u, v, sgn);
                if (axis == 0u) {
                    lp = Ccino::Vec3(sgn, u, v);
                } else if (axis == 1u) {
                    lp = Ccino::Vec3(u, sgn, v);
                }

                Ccino::Vec3 origin = cards[cardIdx].origin;
                Ccino::Vec3 ext    = cards[cardIdx].extent;
                Ccino::Vec3 wp = origin
                    + cards[cardIdx].axisX * (lp.x * ext.x)
                    + cards[cardIdx].axisY * (lp.y * ext.y)
                    + cards[cardIdx].axisZ * (lp.z * ext.z);

                Ccino::Vec3 rgb = (int32_t(cardIdx) == selectedCard)
                    ? Ccino::Vec3(1.0f, 0.95f, 0.35f)
                    : DirectionColor(cDir);
                float alpha = (face == 5u) ? Ccino::Min(baseAlpha * 2.2f, 0.55f) : baseAlpha; // 近投影面稍亮
                col = Ccino::Vec4(rgb, alpha);
                Ccino::Position = vp * Ccino::Vec4(wp, 1.0f);
            };

            auto fs = [=](const Ccino::Vec4& col, Ccino::Vec4& outColor) {
                outColor = col;
            };

            auto rt = rg.RTView(colorRID, pass, Ccino::LoadOp::Load);
            auto dt = rg.RTView(depthRID, pass, Ccino::LoadOp::Load);
            rg.AddVSAndFS(vs, fs, Ccino::GraphicsSetting{
                .drawCounts = topK * 36u,
                .cullMode = Ccino::CullMode::None,
                .depthTestEnable = true,
                .depthWriteEnable = false,
                .blendMode = Ccino::BlendMode::Alpha,
            }, {rt}, &dt);
        });

        // Pass D: Top-K MeshCard OBB 线框 + 朝外法线指示线 (28 顶点/卡片 = 12 条边 + 1 条法线 + 1 条近面对角线)
        builder->AddPass<"MeshCardWirePass">([&](Ccino::RGBuilder& rg, RID& pass) {
            const uint32_t topK = generator.GetStats().topKCards;
            if (!viewOpts.showCardWire || topK == 0) {
                return;
            }
            const float aspect = curHeight > 0 ? static_cast<float>(curWidth) / static_cast<float>(curHeight) : 1.0f;
            Ccino::Mat4 vp = camera.ViewProj(aspect);
            auto cards = rg.Read<MeshCardGen::MeshCardGPU>(cardsRID, pass);
            int32_t dirFilter = viewOpts.dirFilter;
            int32_t selectedCard = viewOpts.selectedCard;

            auto vs = [=](Ccino::Vec3& col) {
                uint32_t cardIdx = uint32_t(Ccino::VertexIndex) / 28u;
                uint32_t localV  = uint32_t(Ccino::VertexIndex) % 28u;
                uint32_t seg     = localV / 2u;
                uint32_t endPt   = localV % 2u;

                uint32_t cActive = cards[cardIdx].isActive;
                uint32_t cDir    = cards[cardIdx].dirIndex;
                bool visible = (cActive != 0u);
                if (dirFilter >= 0 && int32_t(cDir) != dirFilter) {
                    visible = false;
                }
                if (selectedCard >= 0 && int32_t(cardIdx) != selectedCard) {
                    visible = false;
                }
                if (!visible) {
                    col = Ccino::Vec3(0.0f);
                    Ccino::Position = Ccino::Vec4(2.0f, 2.0f, 2.0f, 1.0f);
                    return;
                }

                Ccino::Vec3 origin = cards[cardIdx].origin;
                Ccino::Vec3 ext    = cards[cardIdx].extent;
                Ccino::Vec3 ax     = cards[cardIdx].axisX;
                Ccino::Vec3 ay     = cards[cardIdx].axisY;
                Ccino::Vec3 az     = cards[cardIdx].axisZ;

                Ccino::Vec3 wp = origin;
                if (seg < 12u) {
                    uint32_t axis = seg / 4u;
                    uint32_t k = seg % 4u;
                    float a = ((k & 1u) != 0u) ? 1.0f : -1.0f;
                    float b = ((k & 2u) != 0u) ? 1.0f : -1.0f;
                    float e = (endPt == 0u) ? -1.0f : 1.0f;

                    Ccino::Vec3 lp(e, a, b);
                    if (axis == 1u) {
                        lp = Ccino::Vec3(a, e, b);
                    } else if (axis == 2u) {
                        lp = Ccino::Vec3(a, b, e);
                    }
                    wp = origin
                       + ax * (lp.x * ext.x)
                       + ay * (lp.y * ext.y)
                       + az * (lp.z * ext.z);
                } else if (seg == 12u) {
                    // 卡片近面中心向外的法线指示线
                    float extra = (endPt == 0u) ? 0.0f : 0.28f;
                    wp = origin + az * (ext.z + extra);
                } else {
                    // 卡片近投影面对角线 (标识哪一面是近平面)
                    float d = (endPt == 0u) ? -1.0f : 1.0f;
                    wp = origin + ax * (d * ext.x) + ay * (d * ext.y) + az * ext.z;
                }

                col = (int32_t(cardIdx) == selectedCard)
                    ? Ccino::Vec3(1.0f, 0.98f, 0.40f)
                    : DirectionColor(cDir);
                Ccino::Position = vp * Ccino::Vec4(wp, 1.0f);
            };

            auto fs = [=](const Ccino::Vec3& col, Ccino::Vec4& outColor) {
                outColor = Ccino::Vec4(col, 1.0f);
            };

            auto rt = rg.RTView(colorRID, pass, Ccino::LoadOp::Load);
            auto dt = rg.RTView(depthRID, pass, Ccino::LoadOp::Load);
            rg.AddVSAndFS(vs, fs, Ccino::GraphicsSetting{
                .drawCounts = topK * 28u,
                .topology = Ccino::PrimitiveTopology::LineList,
                .depthTestEnable = true,
                .depthWriteEnable = false,
            }, {rt}, &dt);
        });

        // 4. 运行渲染主循环 + ImGui 操作面板
        builder->LoopRun(colorRID, Ccino::LoopHooks{
            .onEvent = [&](SDL_Event& event) {
                if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
                    return true;
                }
                camera.HandleEvent(event);
                return false;
            },
            .onResize = [&](uint32_t w, uint32_t h) {
                if (w > 0 && h > 0 && (w != curWidth || h != curHeight)) {
                    curWidth = w;
                    curHeight = h;
                    colorRT = createColorRT(curWidth, curHeight);
                    depthRT = createDepthRT(curWidth, curHeight);
                    builder->UpdateExternalTexture2D(colorRID, colorRT);
                    builder->UpdateExternalTexture2D(depthRID, depthRT);
                }
            },
            .onUpdate = [&](Ccino::FrameContext& frame) {
                if (pendingRepeat > 0) {
                    generator.Generate(ctx, genSettings, pendingRepeat);
                    pendingRepeat = 0;
                    if (viewOpts.selectedCard >= static_cast<int>(generator.GetStats().topKCards)) {
                        viewOpts.selectedCard = -1;
                    }
                }
                if (maxFrames != 0 && frame.frameIndex >= maxFrames) {
                    frame.shouldExit = true;
                }
            },
            .onGUI = [&](Ccino::FrameContext&) {
                const auto& stats = generator.GetStats();
                ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_FirstUseEver);
                ImGui::Begin("MeshCard GPU Gen");

                ImGui::Text("GPU Gen Time : %.3f ms", stats.lastGpuMs);
                ImGui::Text("Avg Time     : %.3f ms (runs: %u)", stats.avgGpuMs, stats.runCount);
                if (ImGui::Button("Regenerate")) {
                    pendingRepeat = 1;
                }
                ImGui::SameLine();
                if (ImGui::Button("Benchmark x10")) {
                    pendingRepeat = 10;
                }
                ImGui::SameLine();
                ImGui::Checkbox("Auto", &viewOpts.autoRegenerate);

                ImGui::Separator();
                bool changed = false;
                changed |= ImGui::SliderInt("Max Voxels", &genSettings.maxVoxels, 4, static_cast<int>(MeshCardGen::kMaxVoxels));
                changed |= ImGui::SliderInt("Top-K Cards", &genSettings.maxMeshCards, 1, 12);
                changed |= ImGui::SliderInt("Rays / Cell", &genSettings.raysPerCell, 4, 32);
                changed |= ImGui::SliderInt("Vis Rays", &genSettings.visRays, 4, static_cast<int>(MeshCardGen::kNumHemiRays));
                changed |= ImGui::SliderFloat("Normal Thresh", &genSettings.normalThreshold, 0.0f, 0.9f, "%.2f");
                changed |= ImGui::SliderFloat("Min Density", &genSettings.minDensity, 0.0f, 0.5f, "%.2f");
                changed |= ImGui::SliderFloat("Min Coverage", &genSettings.minClusterCoverage, 0.5f, 20.0f, "%.1f");
                if (changed && viewOpts.autoRegenerate) {
                    pendingRepeat = 1;
                }

                ImGui::Separator();
                ImGui::Checkbox("Sphere", &viewOpts.showSphere);
                ImGui::SameLine();
                ImGui::Checkbox("Surfels", &viewOpts.showSurfels);
                ImGui::SameLine();
                ImGui::Checkbox("Wire", &viewOpts.showCardWire);
                ImGui::SameLine();
                ImGui::Checkbox("Fill", &viewOpts.showCardFill);

                const char* colorModes[] = {"Direction", "WeightedCoverage", "Top-K Card"};
                ImGui::Combo("Surfel Color", &viewOpts.surfelColorMode, colorModes, 3);

                const char* dirFilterItems[] = {"All", "-X", "+X", "-Y", "+Y", "-Z", "+Z"};
                int filterIdx = viewOpts.dirFilter + 1;
                if (ImGui::Combo("Direction", &filterIdx, dirFilterItems, 7)) {
                    viewOpts.dirFilter = filterIdx - 1;
                }
                ImGui::SliderFloat("Surfel Scale", &viewOpts.surfelScale, 0.2f, 1.0f, "%.2f");

                ImGui::Separator();
                ImGui::Text("Surfels: %u | Candidates: %u | Top-K: %u",
                            stats.totalSurfels, stats.candidateCards, stats.topKCards);

                if (ImGui::Button("Show All Cards")) {
                    viewOpts.selectedCard = -1;
                }
                for (size_t i = 0; i < stats.cards.size(); ++i) {
                    const auto& c = stats.cards[i];
                    const bool isSel = (viewOpts.selectedCard == static_cast<int>(i));
                    char label[128];
                    std::snprintf(label, sizeof(label), "#%zu [%s] wCov=%.1f dens=%.2f surf=%u",
                                  i, kDirNames[c.dirIndex % 6u], c.weightedCoverage, c.density, c.surfelCount);
                    if (ImGui::Selectable(label, isSel)) {
                        viewOpts.selectedCard = isSel ? -1 : static_cast<int>(i);
                    }
                }

                ImGui::End();
            },
        });

        const auto& finalStats = generator.GetStats();
        std::cout << "MeshCardGen finished: surfels=" << finalStats.totalSurfels
                  << " candidates=" << finalStats.candidateCards
                  << " topK=" << finalStats.topKCards
                  << " lastGpuMs=" << finalStats.lastGpuMs << " ms" << std::endl;

        generator.Destroy(ctx);
        colorRT.Reset();
        depthRT.Reset();
        sphereTLAS.Reset();
        sphereBLAS.Reset();
        sphereVB.Reset();
        sphereIB.Reset();
        ctx.DestroyRGBuilder("MeshCardVisualizer");
        ctx.Destroy();
        return 0;
    }
} // namespace

int main(int argc, char** argv) {
    const uint64_t maxFrames = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 0;
    try {
        return RunDemo(maxFrames);
    } catch (const std::exception& e) {
        std::cerr << "MeshCardGen failed: " << e.what() << "\n";
        return 1;
    }
}
