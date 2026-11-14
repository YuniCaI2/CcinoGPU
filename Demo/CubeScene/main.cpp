#include "Accessor.h"
#include "AccessorEnums.h"
#include "Camera.h"
#include "Context/CcinoContext.h"
#include "Core/Buffer.h"
#include "Core/CcinoType.h"
#include "Core/Texture2D.h"
#include "Pass.h"
#include "RG/RenderGraph.h"
#include "RefCounted.hpp"
#include "Viewer.h"
#include "glm/ext/matrix_clip_space.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <glm/gtc/matrix_transform.hpp>

#include "CubeMesh.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>

constexpr uint32_t kWidth = 800;
constexpr uint32_t kHeight = 600;
constexpr Ccino::ClearColor kClearColor{{0.1f, 0.1f, 0.1f, 1.0f}};

int RunDemo() {
    Ccino::Context ctx;
    RHISettings settings{};
    settings.bUseSwapChain = true;
    settings.flightFrameCount = 2;
    settings.windowWidth = kWidth;
    settings.windowHeight = kHeight;
    settings.bVsync = false;
    if (!ctx.Init(settings)) {
        std::cerr << "Failed to initialize Ccino context\n";
        return 1;
    }

    Ccino::Ref<Ccino::Buffer> vertexBuffer = ctx.CreateBuffer(Ccino::Buffer::CreateArgs{
        .size = static_cast<uint32_t>(sizeof(kCubeVertices)),
    });
    vertexBuffer->Copy(kCubeVertices.data());
    vertexBuffer->Flush();

    Ccino::Ref<Ccino::Buffer> indexBuffer = ctx.CreateBuffer(Ccino::Buffer::CreateArgs{
        .size = static_cast<uint32_t>(sizeof(kCubeIndices)),
    });
    indexBuffer->Copy(kCubeIndices.data());
    indexBuffer->Flush();

    Ccino::Ref<Ccino::Texture2D> rt = ctx.CreateTexture2D(Ccino::Texture2D::CreateArgs{
            .width = kWidth,
            .height = kHeight,
            .mipLevels = 1,
            .format = Ccino::TextureFormat::RGBA8_sRGB,
            .usage = Ccino::TextureUsage::ColorAttachment | Ccino::TextureUsage::TransferSrc,
    });

    Ccino::Ref<Ccino::Texture2D> depth = ctx.CreateTexture2D(Ccino::Texture2D::CreateArgs{
            .width = kWidth,
            .height = kHeight,
            .mipLevels = 1,
            .format = Ccino::TextureFormat::D32_Float,
            .usage = Ccino::TextureUsage::DepthAttachment | Ccino::TextureUsage::TransferSrc,
    });

    Ccino::RGBuilder* builder = ctx.CreateRGBuilder("CubeScene");
    RID vertexBufferRID = builder->CreateExternalBuffer(vertexBuffer);
    RID indexBufferRID = builder->CreateExternalBuffer(indexBuffer);
    RID targetRID = builder->CreateExternalTexture2D(rt);
    RID depthRID = builder->CreateExternalTexture2D(depth);

    // 立方体在原点、边长 1, 相机放在 -Z 外侧才能看到朝外的正面
    Camera camera({0.0f, 0.4f, -2.5f}, 90.0f, static_cast<float>(kWidth) / kHeight);

    builder->OnEvent([&](SDL_Event& event) {
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
            return true;
        }
        camera.Update(event);
        return false;
    });
    builder->OnUpdate([&](Ccino::FrameContext& frame) {
        camera.UpdateMovement(frame.dt);
    });

    Ccino::GraphicsSetting graphicsSetting{
        .drawCounts = static_cast<uint32_t>(kCubeIndices.size()),
        .cullMode = Ccino::CullMode::Back,
        .depthTestEnable = true,
        .depthWriteEnable = true,
    };
    // 立方体底面在 y = -0.5. 先铺满屏幕的三角形, 片元里用相机射线和这个平面求交, 交点可以超出远平面
    builder->AddPass<"GroundPlanePass">([&](Ccino::RGBuilder& rg, RID& pass) {
        float width = static_cast<float>(kWidth);
        float height = static_cast<float>(kHeight);
        auto viewProj = camera.ViewProj();
        auto invViewProj = glm::inverse(viewProj);
        auto cameraPos = camera.Position();
        Ccino::Vec3 planePoint(0.0f, -0.5f, 0.0f);
        Ccino::Vec3 planeNormal(0.0f, 1.0f, 0.0f);
        Ccino::Vec3 lightDir = Ccino::Normalize(Ccino::Vec3(0.4f, 0.8f, 0.5f));

        auto vs = []() {
            Ccino::Vec2 pos(-1.0f, -1.0f);
            if (Ccino::VertexIndex == 1) {
                pos = Ccino::Vec2(3.0f, -1.0f);
            }
            if (Ccino::VertexIndex == 2) {
                pos = Ccino::Vec2(-1.0f, 3.0f);
            }
            Ccino::Position = Ccino::Vec4(pos, 0.0f, 1.0f);
        };

        auto fs = [=](Ccino::Vec4& color) {
            Ccino::Vec2 ndc(Ccino::FragCoord.x / width * 2.0f - 1.0f, Ccino::FragCoord.y / height * 2.0f - 1.0f);
            Ccino::Vec4 worldH = invViewProj * Ccino::Vec4(ndc, 1.0f, 1.0f);
            Ccino::Vec3 dir = Ccino::Normalize(Ccino::Vec3(worldH.x, worldH.y, worldH.z) / worldH.w - cameraPos);
            float denom = Ccino::Dot(dir, planeNormal);
            float t = 0.0f;
            if (Ccino::Abs(denom) > 0.000001f) {
                t = Ccino::Dot(planePoint - cameraPos, planeNormal) / denom;
            }
            if (t > 0.0f) {
                Ccino::Vec3 p = cameraPos + dir * t;
                float cell = Ccino::Floor(p.x * 2.0f) + Ccino::Floor(p.z * 2.0f);
                Ccino::Vec3 ground(0.62f, 0.63f, 0.64f);
                if (Ccino::Mod(cell, 2.0f) < 1.0f) {
                    ground = Ccino::Vec3(0.32f, 0.34f, 0.36f);
                }
                float shade = Ccino::Max(Ccino::Dot(planeNormal, lightDir), 0.15f);
                float fade = Ccino::SmoothStep(8.0f, 40.0f, t);
                ground = Ccino::Mix(ground * shade, Ccino::Vec3(0.1f, 0.1f, 0.1f), fade);
                color = Ccino::Vec4(ground, 1.0f);
                Ccino::Vec4 clip = viewProj * Ccino::Vec4(p, 1.0f);
                // 略推远, 和底面共面时立方体仍然盖住接触边
                Ccino::FragDepth = Ccino::Min(clip.z / clip.w + 0.00015f, 1.0f);
            } else {
                color = Ccino::Vec4(0.1f, 0.1f, 0.1f, 1.0f);
                Ccino::FragDepth = 1.0f;
            }
        };

        auto renderTarget = rg.RTView(targetRID, pass, Ccino::LoadOp::Clear, kClearColor);
        auto depthRT = rg.RTView(depthRID, pass, Ccino::LoadOp::Clear, Ccino::ClearDepthStencil{1.0f, 0});
        rg.AddVSAndFS(vs, fs, Ccino::GraphicsSetting{
            .drawCounts = 3,
            .depthTestEnable = true,
            .depthWriteEnable = true,
        }, {renderTarget}, &depthRT);
    });
    builder->AddPass<"CubeScenePass">([&](Ccino::RGBuilder& rg, RID& pass) {
        auto mvp = camera.ViewProj();
        auto vb = rg.Read<CubeVertex>(vertexBufferRID, pass);
        auto ib = rg.Read<CubeIndex>(indexBufferRID, pass);
        auto renderTarget = rg.RTView(targetRID, pass, Ccino::LoadOp::Load);
        auto depthRT = rg.RTView(depthRID, pass, Ccino::LoadOp::Load);
        auto vs = [=](Ccino::Vec3& normal) {
            uint32_t index = ib[Ccino::VertexIndex].index;
            normal = vb[index].normal;
            Ccino::Position = mvp * Ccino::Vec4(vb[index].position, 1.0f);
        };
        auto fs = [=](const Ccino::Vec3& normal, Ccino::Vec4& color) {
            float light = Ccino::Max(Ccino::Dot(Ccino::Normalize(normal), Ccino::Normalize(Ccino::Vec3(0.4f, 0.8f, 0.5f))), 0.15f);
            color = Ccino::Vec4(Ccino::Vec3(0.75f) * light, 1.0f);
        };

        rg.AddVSAndFS(vs, fs, graphicsSetting, {renderTarget}, &depthRT);
    });

    builder->LoopRun(targetRID);
    vertexBuffer.Reset();
    indexBuffer.Reset();
    rt.Reset();
    depth.Reset();
    ctx.DestroyRGBuilder("CubeScene");
    ctx.Destroy();
    return 0;
}

int main() {
    try {
        return RunDemo();
    } catch (const std::exception& e) {
        std::cerr << "CubeScene failed: " << e.what() << "\n";
        return 1;
    }
}
