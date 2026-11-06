// 离线绘制一个三角形: 颜色管线渲染到 RGBA8 纹理, 读回后写成 BMP, 不依赖任何图片库
#include "Context/CcinoContext.h"
#include "Core/CcinoType.h"
#include "Core/Texture2D.h"
#include "RG/RenderGraph.h"

#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace {
    constexpr uint32_t kWidth = 512;
    constexpr uint32_t kHeight = 512;
    constexpr Ccino::ClearColor kClearColor{{0.1f, 0.1f, 0.1f, 1.0f}};

    // 24 位 BMP, 行按 4 字节对齐, 从下往上存储; 输入是从上往下紧密排列的 RGBA8
    bool WriteBmp(const std::filesystem::path& path, uint32_t width, uint32_t height, const std::vector<uint8_t>& rgba) {
        const uint32_t rowSize = (width * 3 + 3) & ~3u;
        const uint32_t pixelDataSize = rowSize * height;
        constexpr uint32_t kHeaderSize = 14 + 40;

        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }
        auto write16 = [&](uint16_t value) {
            file.put(static_cast<char>(value & 0xFF));
            file.put(static_cast<char>(value >> 8));
        };
        auto write32 = [&](uint32_t value) {
            for (int i = 0; i < 4; ++i) {
                file.put(static_cast<char>((value >> (8 * i)) & 0xFF));
            }
        };

        // BITMAPFILEHEADER
        file.put('B');
        file.put('M');
        write32(kHeaderSize + pixelDataSize);
        write16(0);
        write16(0);
        write32(kHeaderSize);

        // BITMAPINFOHEADER
        write32(40);
        write32(width);
        write32(height);
        write16(1);  // planes
        write16(24); // bits per pixel
        write32(0);  // BI_RGB, 不压缩
        write32(pixelDataSize);
        write32(2835); // 72 DPI
        write32(2835);
        write32(0);
        write32(0);

        std::vector<char> row(rowSize, 0);
        for (uint32_t y = 0; y < height; ++y) {
            const uint32_t srcY = height - 1 - y;
            for (uint32_t x = 0; x < width; ++x) {
                const uint8_t* pixel = &rgba[(static_cast<size_t>(srcY) * width + x) * 4];
                row[x * 3 + 0] = static_cast<char>(pixel[2]); // BMP 是 BGR
                row[x * 3 + 1] = static_cast<char>(pixel[1]);
                row[x * 3 + 2] = static_cast<char>(pixel[0]);
            }
            file.write(row.data(), rowSize);
        }
        return static_cast<bool>(file);
    }

    const uint8_t* PixelAt(const std::vector<uint8_t>& rgba, uint32_t x, uint32_t y) {
        return &rgba[(static_cast<size_t>(y) * kWidth + x) * 4];
    }

    int RunDemo(const std::filesystem::path& outputPath) {
        Ccino::Context ctx;
        RHISettings settings{};
        settings.bUseSwapChain = false;
        settings.flightFrameCount = 1;
        if (!ctx.Init(settings)) {
            std::cerr << "Failed to initialize Ccino context\n";
            return 1;
        }

        // 外部纹理: RenderGraph 执行完后还要读回
        Ccino::Ref<Ccino::Texture2D> target = ctx.CreateTexture2D(Ccino::Texture2D::CreateArgs{
            .width = kWidth,
            .height = kHeight,
            .mipLevels = 1,
            .format = Ccino::TextureFormat::RGBA8_UNorm,
            .usage = Ccino::TextureUsage::ColorAttachment | Ccino::TextureUsage::TransferSrc,
        });

        Ccino::RGBuilder* builder = ctx.CreateRGBuilder("OfflineTriangle");
        RID targetRID = builder->CreateExternalTexture2D(target);

        builder->AddPass<"OfflineTrianglePass">([&](Ccino::RGBuilder& rg, RID& passRID) {
            Ccino::Vec4 red(1.0f, 0.0f, 0.0f, 1.0f);
            Ccino::Vec4 green(0.0f, 1.0f, 0.0f, 1.0f);
            Ccino::Vec4 blue(0.0f, 0.0f, 1.0f, 1.0f);

            // 不使用顶点缓冲, 按 VertexIndex 生成三个顶点; Vulkan 裁剪空间 y 向下, 第一个顶点在上方
            auto vs = [=](Ccino::Vec4& color) {
                Ccino::Position = Ccino::Vec4(0.0f, -0.5f, 0.0f, 1.0f);
                color = red;
                if (Ccino::VertexIndex == 1) {
                    Ccino::Position = Ccino::Vec4(0.5f, 0.5f, 0.0f, 1.0f);
                    color = green;
                }
                if (Ccino::VertexIndex == 2) {
                    Ccino::Position = Ccino::Vec4(-0.5f, 0.5f, 0.0f, 1.0f);
                    color = blue;
                }
            };
            auto fs = [=](const Ccino::Vec4& color, Ccino::Vec4& outColor) {
                outColor = color;
            };

            auto renderTarget = rg.RTView(targetRID, passRID, Ccino::LoadOp::Clear, kClearColor);
            rg.AddVSAndFS(vs, fs, Ccino::GraphicsSetting{.drawCounts = 3}, {renderTarget});
        });

        if (!ctx.Run("OfflineTriangle")) {
            std::cerr << "Failed to run render graph\n";
            return 1;
        }

        std::vector<uint8_t> pixels(static_cast<size_t>(kWidth) * kHeight * 4);
        if (!target->Readback(pixels.data())) {
            std::cerr << "Failed to read back render target\n";
            return 1;
        }

        if (!WriteBmp(outputPath, kWidth, kHeight, pixels)) {
            std::cerr << "Failed to write " << outputPath << "\n";
            return 1;
        }

        // 粗略自检: 角落应是清屏色, 三角形内部不应是清屏色
        const uint8_t* corner = PixelAt(pixels, 0, 0);
        const uint8_t* inside = PixelAt(pixels, kWidth / 2, kHeight / 2);
        const bool cornerIsClear = corner[0] == corner[1] && corner[1] == corner[2] && corner[0] < 64;
        const bool insideIsDrawn = !(inside[0] == corner[0] && inside[1] == corner[1] && inside[2] == corner[2]);
        std::cout << "Center pixel RGBA = (" << int(inside[0]) << ", " << int(inside[1]) << ", "
                  << int(inside[2]) << ", " << int(inside[3]) << ")" << std::endl;
        std::cout << "Wrote " << std::filesystem::absolute(outputPath).string() << std::endl;

        target.Reset();
        ctx.DestroyRGBuilder("OfflineTriangle");
        ctx.Destroy();

        if (!cornerIsClear || !insideIsDrawn) {
            std::cerr << "Unexpected image content, the triangle may not have been drawn\n";
            return 1;
        }
        return 0;
    }
}

int main(int argc, char** argv) {
    const std::filesystem::path outputPath = argc > 1 ? std::filesystem::path(argv[1]) : "triangle.bmp";
    try {
        return RunDemo(outputPath);
    } catch (const std::exception& e) {
        std::cerr << "OfflineTriangle failed: " << e.what() << "\n";
        return 1;
    }
}
