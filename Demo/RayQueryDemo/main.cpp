// RayQuery Path Tracing Demo:
//   1. 通过 LoadModel 加载 Khronos DamagedHelmet.glb (几何 + 全套 PBR 贴图) 构建 BLAS, 并与地面 Ground BLAS 组合成 TLAS
//   2. Pass 1 (RayQueryPathTracePass): 全屏 FS 发射多跳路径追踪光线 (GGX + Cosine 重要性采样 + 俄罗斯轮盘赌),
//      通过 Alpha 混合将线性 HDR 均值渐进累积到 RGBA16_Float 缓冲
//   3. Pass 2 (RayQueryToneMapPass): 对累积后的线性 HDR 缓冲执行 ACES 色调映射, 输出到 RGBA8_sRGB 后 Blit 到交换链
// 用法: RayQueryDemo [帧数], 不传或传 0 时一直运行到关闭窗口或按 Esc
#include "Context/CcinoContext.h"
#include "Core/AccelerationStructure.h"
#include "Core/Buffer.h"
#include "Core/CcinoType.h"
#include "Core/Texture2D.h"
#include "RG/RenderGraph.h"
#include "Demo/Common/Camera.h"
#include "Demo/RayQueryDemo/LoadModel.h"
#include "Demo/RayQueryDemo/Material.h"
#include "Demo/RayQueryDemo/PathTracing.h"

#include <SDL3/SDL_events.h>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

namespace {
    constexpr uint32_t kWidth = 960;
    constexpr uint32_t kHeight = 600;
    constexpr uint32_t kMaxSpp = 2048;
    constexpr int kMaxBounce = 6;
    constexpr Ccino::ClearColor kClearBlack{{0.0f, 0.0f, 0.0f, 0.0f}};

    struct PositionVertex {
        float x, y, z;
    };

    struct FloatElem {
        float value;
    };

    struct IndexElem {
        uint32_t value;
    };

    int RunDemo(uint64_t maxFrames) {
        Ccino::Context ctx;
        Camera camera({0.0f, 0.45f, -2.8f}, 90.0f, kWidth * 1.0f / kHeight);
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

        // 1. 加载 DamagedHelmet.glb 模型 (顶点缓冲 + 索引缓冲 + PBR 贴图)
        const std::string helmetPath = std::string(CCINO_PROJECT_DIR) + "/Demo/RayQueryDemo/Assets/DamagedHelmet.glb";
        auto helmetModel = LoadModel(
            ctx,
            helmetPath,
            VertexTypeFlags(VertexTypeFlagBits::POSITION) | VertexTypeFlagBits::NORMAL | VertexTypeFlagBits::TEXCOORD);
        if (!helmetModel || helmetModel->meshs.empty()) {
            std::cerr << "Failed to load DamagedHelmet model: " << helmetPath << "\n";
            ctx.Destroy();
            return 1;
        }

        const Mesh& helmetMesh = helmetModel->meshs[0];
        const glm::ivec4& posView = helmetMesh.bufferViews[0];
        const glm::ivec4& normView = helmetMesh.bufferViews[1];
        const glm::ivec4& uvView = helmetMesh.bufferViews[2];
        const auto& helmetVB = helmetModel->vertexBuffers[posView[0]];
        const auto& helmetIB = helmetModel->meshBuffers[0];

        // 旋转 180° 让头盔面罩朝向 -Z (面向相机), 并抬高使其底面贴合 y = -0.5 地面
        const Ccino::Mat4 helmetWorldMat =
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.42f, 0.0f)) *
            glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 1.0f, 0.0f)) *
            helmetMesh.trans;
        const Ccino::Mat3 helmetNormalMat = glm::transpose(glm::inverse(glm::mat3(helmetWorldMat)));

        auto helmetBLAS = ctx.CreateBLAS({
            .vertexBuffer  = helmetVB,
            .vertexOffset  = static_cast<uint32_t>(posView[2]),
            .vertexStride  = static_cast<uint32_t>(posView[1]),
            .vertexCount   = static_cast<uint32_t>(posView[3]),
            .indexType     = Ccino::RHI::IndexType::e32,
            .indexBuffer   = helmetIB,
            .indexOffset   = 0,
            .indexCount    = static_cast<uint32_t>(helmetIB->GetSize() / sizeof(uint32_t)),
            .geometryFlags = Ccino::RHI::GeometryFlagBits::QPAQUE,
        });

        // 取出 5 张 PBR 贴图 (缺失槽位用 1x1 默认贴图兜底)
        auto resolveTex = [&](TextureType slot, std::array<uint8_t, 4> fallbackRgba, Ccino::TextureFormat fmt) {
            const int idx = helmetMesh.textureIndices[static_cast<size_t>(slot)];
            if (idx >= 0 && static_cast<size_t>(idx) < helmetModel->textures.size() && helmetModel->textures[idx]) {
                return helmetModel->textures[idx];
            }
            return PBR::CreateSolidTexture2D(ctx, fallbackRgba, fmt);
        };
        auto baseColorTexture = resolveTex(TextureType::BASE_COLOR, {255, 255, 255, 255}, Ccino::TextureFormat::RGBA8_sRGB);
        auto normalTexture    = resolveTex(TextureType::NORMAL,     {128, 128, 255, 255}, Ccino::TextureFormat::RGBA8_UNorm);
        auto mrTexture        = resolveTex(TextureType::ROUGHNESS,  {255, 255, 255, 255}, Ccino::TextureFormat::RGBA8_UNorm);
        auto aoTexture        = resolveTex(TextureType::AO,         {255, 255, 255, 255}, Ccino::TextureFormat::RGBA8_UNorm);
        auto emissiveTexture  = resolveTex(TextureType::EMISSIVE,   {0,   0,   0,   255}, Ccino::TextureFormat::RGBA8_sRGB);

        // 2. 构建地面平面 BLAS
        const std::vector<PositionVertex> planeVertices = {
            {-5.0f, 0.0f, -5.0f},
            { 5.0f, 0.0f, -5.0f},
            { 5.0f, 0.0f,  5.0f},
            {-5.0f, 0.0f,  5.0f},
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

        // 3. 组合成 TLAS (地面 + DamagedHelmet)
        auto tlas = ctx.CreateTLAS({
            .instances = {
                {.transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.5f, 0.0f)), .blas = planeBLAS, .customIndex = 0, .bCulling = false},
                {.transform = helmetWorldMat, .blas = helmetBLAS, .customIndex = 1, .bCulling = false},
            }
        });

        // 4. 离屏渲染目标:
        //    - accumTarget (RGBA16_Float): 线性 HDR 渐进累积缓冲
        //    - target      (RGBA8_sRGB):   ToneMap 后的输出纹理, 最后 Blit 到交换链
        uint32_t curWidth = kWidth;
        uint32_t curHeight = kHeight;
        bool needResetAccum = true;
        bool clearAccumThisFrame = true;
        uint32_t sampleIndex = 0;

        auto createAccumTarget = [&](uint32_t w, uint32_t h) {
            return ctx.CreateTexture2D(Ccino::Texture2D::CreateArgs{
                .width = w,
                .height = h,
                .mipLevels = 1,
                .format = Ccino::TextureFormat::RGBA16_Float,
                .usage = Ccino::TextureUsage::ColorAttachment | Ccino::TextureUsage::Sampled,
            });
        };
        auto createTarget = [&](uint32_t w, uint32_t h) {
            return ctx.CreateTexture2D(Ccino::Texture2D::CreateArgs{
                .width = w,
                .height = h,
                .mipLevels = 1,
                .format = Ccino::TextureFormat::RGBA8_sRGB,
                .usage = Ccino::TextureUsage::ColorAttachment | Ccino::TextureUsage::TransferSrc,
            });
        };
        Ccino::Ref<Ccino::Texture2D> accumTarget = createAccumTarget(curWidth, curHeight);
        Ccino::Ref<Ccino::Texture2D> target = createTarget(curWidth, curHeight);

        Ccino::RGBuilder* builder = ctx.CreateRGBuilder("RayQueryDemo");
        RID accumRID        = builder->CreateExternalTexture2D(accumTarget);
        RID targetRID       = builder->CreateExternalTexture2D(target);
        RID tlasRID         = builder->CreateExternalTLAS(tlas);
        RID helmetVBRid     = builder->CreateExternalBuffer(helmetVB);
        RID helmetIBRid     = builder->CreateExternalBuffer(helmetIB);
        RID baseColorTexRID = builder->CreateExternalTexture2D(baseColorTexture);
        RID normalTexRID    = builder->CreateExternalTexture2D(normalTexture);
        RID mrTexRID        = builder->CreateExternalTexture2D(mrTexture);
        RID emissiveTexRID  = builder->CreateExternalTexture2D(emissiveTexture);

        const uint32_t posBaseFloats    = static_cast<uint32_t>(posView[2]) / 4u;
        const uint32_t posStrideFloats  = static_cast<uint32_t>(posView[1]) / 4u;
        const uint32_t normBaseFloats   = static_cast<uint32_t>(normView[2]) / 4u;
        const uint32_t normStrideFloats = static_cast<uint32_t>(normView[1]) / 4u;
        const uint32_t uvBaseFloats     = static_cast<uint32_t>(uvView[2]) / 4u;
        const uint32_t uvStrideFloats   = static_cast<uint32_t>(uvView[1]) / 4u;

        const Ccino::Vec3 baseColorFactor(helmetMesh.baseColorFactor);
        const float roughnessFactor = helmetMesh.roughnessFactor;
        const float metallicFactor  = helmetMesh.metallicFactor;
        const Ccino::Vec3 emissiveFactor = helmetMesh.emissiveFactor;

        const auto start = std::chrono::steady_clock::now();

        // 5. Pass 1: 全屏纯 Path Tracing, 通过 Alpha 混合将每帧线性 HDR 样本渐进平均到 accumTarget
        builder->AddPass<"RayQueryPathTracePass">([&](Ccino::RGBuilder& rg, RID& passRID) {
            auto sceneAS      = rg.ReadAS(tlasRID, passRID);
            auto vb           = rg.Read<FloatElem>(helmetVBRid, passRID);
            auto ib           = rg.Read<IndexElem>(helmetIBRid, passRID);
            auto baseColorTex = rg.Sample<Ccino::ViewType::Float>(baseColorTexRID, passRID);
            auto normalTex    = rg.Sample<Ccino::ViewType::Float>(normalTexRID, passRID);
            auto mrTex        = rg.Sample<Ccino::ViewType::Float>(mrTexRID, passRID);
            auto emissiveTex  = rg.Sample<Ccino::ViewType::Float>(emissiveTexRID, passRID);

            const Ccino::Vec3 camPos = camera.Position();
            const Ccino::Vec3 camForward = camera.LookAt();
            const Ccino::Vec3 worldUp(0.0f, 1.0f, 0.0f);
            const Ccino::Vec3 camRight = glm::normalize(glm::cross(camForward, worldUp));
            const Ccino::Vec3 camUp = glm::normalize(glm::cross(camRight, camForward));
            const float aspect = curHeight > 0 ? static_cast<float>(curWidth) / static_cast<float>(curHeight) : 1.0f;
            const Ccino::Vec2 invResolution(
                curWidth > 0 ? 1.0f / static_cast<float>(curWidth) : 0.0f,
                curHeight > 0 ? 1.0f / static_cast<float>(curHeight) : 0.0f);
            uint32_t frameSampleIdx = sampleIndex;
            uint32_t maxSppCount = kMaxSpp;
            int maxBounce = kMaxBounce;

            auto vs = [](Ccino::Vec2& uv) {
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
                // 达到目标 SPP 后输出 alpha = 0, Alpha 混合保持 accumTarget 不变并跳过光追
                if (frameSampleIdx == 0u || frameSampleIdx > maxSppCount) {
                    outColor = Ccino::Vec4(0.0f, 0.0f, 0.0f, 0.0f);
                    return;
                }

                // 每像素每帧独立的 PCG 随机数种子
                uint32_t px = uint32_t(Ccino::FragCoord.x);
                uint32_t py = uint32_t(Ccino::FragCoord.y);
                uint32_t rngState = PBR::PCGHash(px * 1973u + py * 9277u + frameSampleIdx * 26699u + 17u);

                // 亚像素随机抖动 (AA)
                float jx = PBR::NextRand(rngState) - 0.5f;
                float jy = PBR::NextRand(rngState) - 0.5f;
                Ccino::Vec2 jitteredUv = uv + Ccino::Vec2(jx, jy) * invResolution;
                Ccino::Vec2 ndc(jitteredUv.x * 2.0f - 1.0f, 1.0f - jitteredUv.y * 2.0f);
                float tanHalfFov = 0.55f;

                Ccino::Vec3 rayOrigin = camPos;
                Ccino::Vec3 rayDir = Ccino::Normalize(
                    camForward +
                    camRight * (ndc.x * aspect * tanHalfFov) +
                    camUp * (ndc.y * tanHalfFov));

                Ccino::Vec3 radiance(0.0f, 0.0f, 0.0f);
                Ccino::Vec3 throughput(1.0f, 1.0f, 1.0f);

                for (int bounce = 0; bounce < maxBounce; ++bounce) {
                    Ccino::RayQueryEXT rq;
                    Ccino::RayQueryInitializeEXT(
                        rq, sceneAS,
                        Ccino::RayFlagsOpaqueEXT, 0xFFu,
                        rayOrigin, 0.001f, rayDir, 100.0f);
                    while (Ccino::RayQueryProceedEXT(rq)) {
                    }

                    // 未命中物体 -> 采样天空环境光, 路径终止
                    if (Ccino::RayQueryGetIntersectionTypeEXT(rq, true) != Ccino::RayQueryCommittedIntersectionTriangleEXT) {
                        radiance = radiance + throughput * PBR::EvalPathTracingSky(rayDir);
                        break;
                    }

                    float hitT = Ccino::RayQueryGetIntersectionTEXT(rq, true);
                    Ccino::Vec2 bary = Ccino::RayQueryGetIntersectionBarycentricsEXT(rq, true);
                    int32_t instId = Ccino::RayQueryGetIntersectionInstanceCustomIndexEXT(rq, true);
                    int32_t primId = Ccino::RayQueryGetIntersectionPrimitiveIndexEXT(rq, true);
                    Ccino::Vec3 hitPos = rayOrigin + rayDir * hitT;
                    Ccino::Vec3 V = Ccino::Normalize(-rayDir);

                    PBR::Surface surface;
                    surface.normal    = Ccino::Vec3(0.0f, 1.0f, 0.0f);
                    surface.albedo    = Ccino::Vec3(0.5f, 0.5f, 0.5f);
                    surface.roughness = 0.65f;
                    surface.metallic  = 0.0f;
                    surface.ao        = 1.0f;
                    surface.emissive  = Ccino::Vec3(0.0f, 0.0f, 0.0f);

                    if (instId == 0) {
                        if (Ccino::Dot(surface.normal, V) < 0.0f) {
                            surface.normal = -surface.normal;
                        }
                        float cx = Ccino::Floor(hitPos.x * 2.0f);
                        float cz = Ccino::Floor(hitPos.z * 2.0f);
                        float checker = Ccino::Mod(cx + cz, 2.0f);
                        surface.albedo    = checker < 0.5f ? Ccino::Vec3(0.18f, 0.20f, 0.24f) : Ccino::Vec3(0.36f, 0.39f, 0.44f);
                        surface.roughness = checker < 0.5f ? 0.38f : 0.65f;
                        surface.metallic  = 0.05f;
                    } else {
                        float b0 = 1.0f - bary.x - bary.y;
                        float b1 = bary.x;
                        float b2 = bary.y;

                        uint32_t triBase = static_cast<uint32_t>(primId) * 3u;
                        uint32_t i0 = ib[triBase + 0u].value;
                        uint32_t i1 = ib[triBase + 1u].value;
                        uint32_t i2 = ib[triBase + 2u].value;

                        uint32_t p0Off = posBaseFloats + i0 * posStrideFloats;
                        uint32_t p1Off = posBaseFloats + i1 * posStrideFloats;
                        uint32_t p2Off = posBaseFloats + i2 * posStrideFloats;
                        Ccino::Vec3 p0(vb[p0Off].value, vb[p0Off + 1u].value, vb[p0Off + 2u].value);
                        Ccino::Vec3 p1(vb[p1Off].value, vb[p1Off + 1u].value, vb[p1Off + 2u].value);
                        Ccino::Vec3 p2(vb[p2Off].value, vb[p2Off + 1u].value, vb[p2Off + 2u].value);

                        uint32_t n0Off = normBaseFloats + i0 * normStrideFloats;
                        uint32_t n1Off = normBaseFloats + i1 * normStrideFloats;
                        uint32_t n2Off = normBaseFloats + i2 * normStrideFloats;
                        Ccino::Vec3 n0(vb[n0Off].value, vb[n0Off + 1u].value, vb[n0Off + 2u].value);
                        Ccino::Vec3 n1(vb[n1Off].value, vb[n1Off + 1u].value, vb[n1Off + 2u].value);
                        Ccino::Vec3 n2(vb[n2Off].value, vb[n2Off + 1u].value, vb[n2Off + 2u].value);

                        uint32_t uv0Off = uvBaseFloats + i0 * uvStrideFloats;
                        uint32_t uv1Off = uvBaseFloats + i1 * uvStrideFloats;
                        uint32_t uv2Off = uvBaseFloats + i2 * uvStrideFloats;
                        Ccino::Vec2 t0(vb[uv0Off].value, vb[uv0Off + 1u].value);
                        Ccino::Vec2 t1(vb[uv1Off].value, vb[uv1Off + 1u].value);
                        Ccino::Vec2 t2(vb[uv2Off].value, vb[uv2Off + 1u].value);

                        Ccino::Vec2 texUV = t0 * b0 + t1 * b1 + t2 * b2;
                        Ccino::Vec3 Ninterp = Ccino::Normalize(helmetNormalMat * (n0 * b0 + n1 * b1 + n2 * b2));
                        Ccino::Vec3 mapSample = Ccino::Vec3(normalTex.Sample(texUV, Ccino::SamplerType::LINEAR_REPEAT, 0.0f));
                        surface.normal = PBR::ApplyTangentNormalMap(
                            Ninterp, V,
                            helmetNormalMat * (p1 - p0), helmetNormalMat * (p2 - p0),
                            t1 - t0, t2 - t0, mapSample);

                        Ccino::Vec4 mrSample = mrTex.Sample(texUV, Ccino::SamplerType::LINEAR_REPEAT, 0.0f);
                        surface.albedo    = Ccino::Vec3(baseColorTex.Sample(texUV, Ccino::SamplerType::LINEAR_REPEAT, 0.0f)) * baseColorFactor;
                        surface.roughness = Ccino::Clamp(mrSample.y * roughnessFactor, 0.04f, 1.0f);
                        surface.metallic  = Ccino::Clamp(mrSample.z * metallicFactor, 0.0f, 1.0f);
                        surface.emissive  = Ccino::Vec3(emissiveTex.Sample(texUV, Ccino::SamplerType::LINEAR_REPEAT, 0.0f)) * emissiveFactor;
                    }

                    // 累加物体自发光贡献 (Bounce 之间直接相加)
                    radiance = radiance + throughput * surface.emissive * 1.4f;

                    // BSDF 重要性采样下一跳方向
                    PBR::BSDFSample bsdfSample = PBR::SamplePBRBSDF(surface, V, rngState);
                    if (bsdfSample.valid == 0) {
                        break;
                    }
                    throughput = throughput * bsdfSample.weight;

                    // 俄罗斯轮盘赌 (从第 2 次弹射后开始按剩余能量概率提前终止)
                    if (bounce >= 2) {
                        float maxComp = Ccino::Max(throughput.x, Ccino::Max(throughput.y, throughput.z));
                        float q = Ccino::Clamp(maxComp, 0.05f, 0.95f);
                        if (PBR::NextRand(rngState) > q) {
                            break;
                        }
                        throughput = throughput / q;
                    }

                    // 推进光线到下一跳 (沿法线微偏移防止自相交)
                    rayOrigin = hitPos + surface.normal * 0.002f;
                    rayDir = bsdfSample.dir;
                }

                // NaN 防护与温和萤火虫抑制
                if (Ccino::IsNan(radiance.x) || Ccino::IsNan(radiance.y) || Ccino::IsNan(radiance.z)) {
                    radiance = Ccino::Vec3(0.0f, 0.0f, 0.0f);
                }
                radiance = Ccino::Min(radiance, Ccino::Vec3(20.0f, 20.0f, 20.0f));

                // Alpha = 1 / N: 硬件 Alpha 混合自动计算 dst = (1/N) * curr + (1 - 1/N) * prev
                float blendWeight = 1.0f / float(frameSampleIdx);
                outColor = Ccino::Vec4(radiance, blendWeight);
            };

            auto accumView = rg.RTView(
                accumRID, passRID,
                clearAccumThisFrame ? Ccino::LoadOp::Clear : Ccino::LoadOp::Load,
                kClearBlack);
            rg.AddVSAndFS(
                vs, fs,
                Ccino::GraphicsSetting{.drawCounts = 3, .blendMode = Ccino::BlendMode::Alpha},
                {accumView});
        }).AddPass<"RayQueryToneMapPass">([&](Ccino::RGBuilder& rg, RID& passRID) {
            // 6. Pass 2: 采样线性 HDR 累积缓冲, ACES 色调映射到 RGBA8_sRGB
            auto accumTex = rg.Sample<Ccino::ViewType::Float>(accumRID, passRID);

            auto vs = [](Ccino::Vec2& uv) {
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
                Ccino::Vec3 hdrColor = Ccino::Vec3(accumTex.Sample(uv, Ccino::SamplerType::NEAREST_CLAMP_TO_EDGE, 0.0f));
                outColor = Ccino::Vec4(PBR::ToneMapACES(hdrColor), 1.0f);
            };

            auto presentView = rg.RTView(targetRID, passRID, Ccino::LoadOp::DontCare);
            rg.AddVSAndFS(vs, fs, Ccino::GraphicsSetting{.drawCounts = 3}, {presentView});
        });

        uint64_t frameCount = 0;
        builder->LoopRun(targetRID, Ccino::LoopHooks{
            .onEvent = [&](SDL_Event& event) {
                if (camera.Update(event)) {
                    needResetAccum = true;
                }
                return event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE;
            },
            .onResize = [&](uint32_t w, uint32_t h) {
                if (w > 0 && h > 0 && (w != curWidth || h != curHeight)) {
                    curWidth = w;
                    curHeight = h;
                    camera.SetAspect(static_cast<float>(curWidth) / static_cast<float>(curHeight));
                    accumTarget = createAccumTarget(curWidth, curHeight);
                    target = createTarget(curWidth, curHeight);
                    builder->UpdateExternalTexture2D(accumRID, accumTarget);
                    builder->UpdateExternalTexture2D(targetRID, target);
                }
                needResetAccum = true;
            },
            .onUpdate = [&](Ccino::FrameContext& frame) {
                if (camera.UpdateMovement(frame.dt)) {
                    needResetAccum = true;
                }
                clearAccumThisFrame = needResetAccum;
                if (needResetAccum) {
                    sampleIndex = 0;
                    needResetAccum = false;
                }
                if (sampleIndex <= kMaxSpp) {
                    ++sampleIndex;
                }
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

        accumTarget.Reset();
        target.Reset();
        tlas.Reset();
        helmetBLAS.Reset();
        baseColorTexture.Reset();
        normalTexture.Reset();
        mrTexture.Reset();
        aoTexture.Reset();
        emissiveTexture.Reset();
        helmetModel.reset();
        planeBLAS.Reset();
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
