// 窗口绘制一个旋转的三角形: RenderGraph 每帧渲染到离屏 RGBA8 纹理, 再 Blit 到交换链上显示
// 用法: SwapchainTriangle [帧数], 不传或传 0 时一直运行到关闭窗口或按 Esc; 窗口可以调整大小
#include "Context/CcinoContext.h"
#include "Core/CcinoType.h"
#include "Core/Texture2D.h"
#include "RG/RenderGraph.h"

#include <SDL3/SDL_events.h>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>

namespace {
    constexpr uint32_t kWidth = 800;
    constexpr uint32_t kHeight = 600;
    constexpr Ccino::ClearColor kClearColor{{0.1f, 0.1f, 0.1f, 1.0f}};

    int RunDemo(uint64_t maxFrames) {
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

        // sRGB 格式: 着色器输出的线性颜色写入时编码, Blit 到 sRGB 交换链时先解码再编码, 颜色不变
        Ccino::Ref<Ccino::Texture2D> target = ctx.CreateTexture2D(Ccino::Texture2D::CreateArgs{
            .width = kWidth,
            .height = kHeight,
            .mipLevels = 1,
            .format = Ccino::TextureFormat::RGBA8_sRGB,
            .usage = Ccino::TextureUsage::ColorAttachment | Ccino::TextureUsage::TransferSrc,
        });

        Ccino::RGBuilder* builder = ctx.CreateRGBuilder("SwapchainTriangle");
        RID targetRID = builder->CreateExternalTexture2D(target);

        const auto start = std::chrono::steady_clock::now();

        // LoopRun 每帧都会重新调用这个 lambda, angle 作为 uniform 每帧更新
        builder->AddPass<"SwapchainTrianglePass">([&](Ccino::RGBuilder& rg, RID& passRID) {
            Ccino::Vec4 red(1.0f, 0.0f, 0.0f, 1.0f);
            Ccino::Vec4 green(0.0f, 1.0f, 0.0f, 1.0f);
            Ccino::Vec4 blue(0.0f, 0.0f, 1.0f, 1.0f);
            float angle = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();

            // 不使用顶点缓冲, 按 VertexIndex 生成三个顶点; Vulkan 裁剪空间 y 向下, 第一个顶点在上方
            auto vs = [=](Ccino::Vec4& color) {
                Ccino::Vec2 pos(0.0f, -0.5f);
                color = red;
                if (Ccino::VertexIndex == 1) {
                    pos = Ccino::Vec2(0.5f, 0.5f);
                    color = green;
                }
                if (Ccino::VertexIndex == 2) {
                    pos = Ccino::Vec2(-0.5f, 0.5f);
                    color = blue;
                }
                float c = Ccino::Cos(angle);
                float s = Ccino::Sin(angle);
                // 旋转矩阵: GLSL mat2 按列优先构造, 列 0 为 (c, s), 列 1 为 (-s, c)
                Ccino::Mat2 rot(c, s, -s, c);
                Ccino::Position = Ccino::Vec4(rot * pos, 0.0f, 1.0f);
            };
            auto fs = [=](const Ccino::Vec4& color, Ccino::Vec4& outColor) {
                outColor = color;
            };

            auto renderTarget = rg.RTView(targetRID, passRID, Ccino::LoadOp::Clear, kClearColor);
            rg.AddVSAndFS(vs, fs, Ccino::GraphicsSetting{.drawCounts = 3}, {renderTarget});
        });

        uint64_t frameCount = 0;
        builder->LoopRun(targetRID, Ccino::LoopHooks{
            .onEvent = [](SDL_Event& event) {
                return event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE;
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
        ctx.DestroyRGBuilder("SwapchainTriangle");
        ctx.Destroy();
        return 0;
    }
}

int main(int argc, char** argv) {
    const uint64_t maxFrames = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 0;
    try {
        return RunDemo(maxFrames);
    } catch (const std::exception& e) {
        std::cerr << "SwapchainTriangle failed: " << e.what() << "\n";
        return 1;
    }
}
