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

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <imgui.h>
#include <iostream>

constexpr uint32_t kWidth = 800;
constexpr uint32_t kHeight = 600;
constexpr Ccino::ClearColor kClearColor{{0.1f, 0.1f, 0.1f, 1.0f}};

// 创建一个正方体 (24 顶点 / 36 索引, 右手系逆时针正面, 法线朝外)
struct CubeVertex {
    Ccino::Vec3 position;
    Ccino::Vec3 normal;
    Ccino::Vec2 uv;
};

// BDA Accessor<T> 需要结构体以生成 GLSL buffer_reference
struct CubeIndex {
    uint32_t index;
};

constexpr std::array<CubeVertex, 24> kCubeVertices = {{
    // +Z (前)
    {{-0.5f, -0.5f,  0.5f}, { 0.0f,  0.0f,  1.0f}, {0.0f, 1.0f}},
    {{ 0.5f, -0.5f,  0.5f}, { 0.0f,  0.0f,  1.0f}, {1.0f, 1.0f}},
    {{ 0.5f,  0.5f,  0.5f}, { 0.0f,  0.0f,  1.0f}, {1.0f, 0.0f}},
    {{-0.5f,  0.5f,  0.5f}, { 0.0f,  0.0f,  1.0f}, {0.0f, 0.0f}},
    // -Z (后)
    {{ 0.5f, -0.5f, -0.5f}, { 0.0f,  0.0f, -1.0f}, {0.0f, 1.0f}},
    {{-0.5f, -0.5f, -0.5f}, { 0.0f,  0.0f, -1.0f}, {1.0f, 1.0f}},
    {{-0.5f,  0.5f, -0.5f}, { 0.0f,  0.0f, -1.0f}, {1.0f, 0.0f}},
    {{ 0.5f,  0.5f, -0.5f}, { 0.0f,  0.0f, -1.0f}, {0.0f, 0.0f}},
    // -X (左)
    {{-0.5f, -0.5f, -0.5f}, {-1.0f,  0.0f,  0.0f}, {0.0f, 1.0f}},
    {{-0.5f, -0.5f,  0.5f}, {-1.0f,  0.0f,  0.0f}, {1.0f, 1.0f}},
    {{-0.5f,  0.5f,  0.5f}, {-1.0f,  0.0f,  0.0f}, {1.0f, 0.0f}},
    {{-0.5f,  0.5f, -0.5f}, {-1.0f,  0.0f,  0.0f}, {0.0f, 0.0f}},
    // +X (右)
    {{ 0.5f, -0.5f,  0.5f}, { 1.0f,  0.0f,  0.0f}, {0.0f, 1.0f}},
    {{ 0.5f, -0.5f, -0.5f}, { 1.0f,  0.0f,  0.0f}, {1.0f, 1.0f}},
    {{ 0.5f,  0.5f, -0.5f}, { 1.0f,  0.0f,  0.0f}, {1.0f, 0.0f}},
    {{ 0.5f,  0.5f,  0.5f}, { 1.0f,  0.0f,  0.0f}, {0.0f, 0.0f}},
    // +Y (上)
    {{-0.5f,  0.5f,  0.5f}, { 0.0f,  1.0f,  0.0f}, {0.0f, 1.0f}},
    {{ 0.5f,  0.5f,  0.5f}, { 0.0f,  1.0f,  0.0f}, {1.0f, 1.0f}},
    {{ 0.5f,  0.5f, -0.5f}, { 0.0f,  1.0f,  0.0f}, {1.0f, 0.0f}},
    {{-0.5f,  0.5f, -0.5f}, { 0.0f,  1.0f,  0.0f}, {0.0f, 0.0f}},
    // -Y (下)
    {{-0.5f, -0.5f, -0.5f}, { 0.0f, -1.0f,  0.0f}, {0.0f, 1.0f}},
    {{ 0.5f, -0.5f, -0.5f}, { 0.0f, -1.0f,  0.0f}, {1.0f, 1.0f}},
    {{ 0.5f, -0.5f,  0.5f}, { 0.0f, -1.0f,  0.0f}, {1.0f, 0.0f}},
    {{-0.5f, -0.5f,  0.5f}, { 0.0f, -1.0f,  0.0f}, {0.0f, 0.0f}},
}};

constexpr std::array<CubeIndex, 36> kCubeIndices = {{
    {0},  {1},  {2},  {0},  {2},  {3},  // +Z
    {4},  {5},  {6},  {4},  {6},  {7},  // -Z
    {8},  {9},  {10}, {8},  {10}, {11}, // -X
    {12}, {13}, {14}, {12}, {14}, {15}, // +X
    {16}, {17}, {18}, {16}, {18}, {19}, // +Y
    {20}, {21}, {22}, {20}, {22}, {23}, // -Y
}};

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
    builder->AddPass<"CubeScenePass">([&](Ccino::RGBuilder& rg, RID& pass) {
        auto mvp = camera.ViewProj();
        auto vb = rg.Read<CubeVertex>(vertexBufferRID, pass);
        auto ib = rg.Read<CubeIndex>(indexBufferRID, pass);
        auto renderTarget = rg.RTView(targetRID, pass, Ccino::LoadOp::Clear, kClearColor);
        auto depthRT = rg.RTView(depthRID, pass, Ccino::LoadOp::Clear, Ccino::ClearDepthStencil{1.0f, 0});
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