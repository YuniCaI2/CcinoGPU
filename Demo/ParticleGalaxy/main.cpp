// 26 万个粒子在四种奇异吸引子 (Aizawa / Lorenz / Thomas / Halvorsen) 之间流动变形, 每帧四个 Pass:
//   1. ParticleSimulatePass  (Compute)  读写粒子 Buffer: 吸引子速度场 + 鼠标黑洞 + 冲击波 + 寿命重生
//   2. ParticleFadePass      (Graphics) 把上一帧的 HDR 纹理按时间衰减, 留下拖尾
//   3. ParticleDrawPass      (Graphics) VS 直接读粒子 Buffer 展开成面片, 加法混合写入 RGBA16F
//   4. ParticleCompositePass (Graphics) 采样 HDR 纹理做 Bloom + ACES 色调映射, 输出到 sRGB 纹理后 Blit 到交换链
// 用法: ParticleGalaxy [帧数], 不传或传 0 时一直运行
// 操作: 左键按住 黑洞漩涡 | 右键 冲击波 | 滚轮 缩放 | 1-4 / Tab 切换吸引子 | Space 中心爆发 | 上/下 曝光 | R 重置 | Esc 退出
#include "Context/CcinoContext.h"
#include "Core/Buffer.h"
#include "Core/CcinoType.h"
#include "Core/Texture2D.h"
#include "RG/RenderGraph.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>

namespace {
    constexpr uint32_t kWidth = 1280;
    constexpr uint32_t kHeight = 720;
    constexpr uint32_t kParticleCount = 1u << 18;
    constexpr uint32_t kThreadGroupSize = 256;
    constexpr int kAttractorCount = 4;
    constexpr float kMorphSeconds = 2.5f;
    constexpr float kAutoSwitchSeconds = 16.0f;
    constexpr Ccino::ClearColor kBlack{{0.0f, 0.0f, 0.0f, 0.0f}};
    constexpr const char* kAttractorNames[kAttractorCount] = {"Aizawa", "Lorenz", "Thomas", "Halvorsen"};
    constexpr float kAttractorHues[kAttractorCount] = {0.55f, 0.02f, 0.30f, 0.78f};

    // GLSL 端是 scalar 布局的 buffer_reference, 只用 Vec4 保证两边步长一致 (48 字节)
    struct Particle {
        Ccino::Vec4 posAge;    // xyz 位置, w 年龄(秒)
        Ccino::Vec4 driftSeed; // xyz 外力累积出的附加速度, w 粒子固定的随机种子
        Ccino::Vec4 motion;    // x 合速度大小, y 附加速度大小, 供着色使用
    };

    // 以下自由函数会被翻译成 GLSL 函数. 生成器只收集 Shader Lambda 里直接调用的函数, 所以函数之间不要互相调用
    float Hash13(Ccino::Vec3 p3) {
        p3 = Ccino::Fract(p3 * 0.1031f);
        p3 += Ccino::Dot(p3, Ccino::Vec3(p3.z, p3.y, p3.x) + 33.33f);
        return Ccino::Fract((p3.x + p3.y) * p3.z);
    }

    // 返回世界空间速度. 吸引子坐标 q 的 z 轴对应世界 y 轴, 尺度/中心/时间缩放让四个吸引子都落在半径约 1.5 的范围内
    Ccino::Vec3 AttractorVelocity(int mode, Ccino::Vec3 p) {
        Ccino::Vec3 q = Ccino::Vec3(p.x, p.z, p.y);
        Ccino::Vec3 dq = Ccino::Vec3(0.0f);
        float scale = 1.0f;
        float rate = 1.0f;
        if (mode == 0) {
            scale = 0.95f;
            rate = 0.8f;
            q = q / scale + Ccino::Vec3(0.0f, 0.0f, 0.64f);
            float zb = q.z - 0.7f;
            dq.x = zb * q.x - 3.5f * q.y;
            dq.y = 3.5f * q.x + zb * q.y;
            dq.z = 0.6f + 0.95f * q.z - q.z * q.z * q.z / 3.0f - (q.x * q.x + q.y * q.y) * (1.0f + 0.25f * q.z) +
                   0.1f * q.z * q.x * q.x * q.x;
        }
        if (mode == 1) {
            scale = 0.06f;
            rate = 0.25f;
            q = q / scale + Ccino::Vec3(0.0f, 0.0f, 25.0f);
            dq.x = 10.0f * (q.y - q.x);
            dq.y = q.x * (28.0f - q.z) - q.y;
            dq.z = q.x * q.y - 2.6666667f * q.z;
        }
        if (mode == 2) {
            scale = 0.37f;
            rate = 4.0f;
            q = q / scale;
            dq.x = Ccino::Sin(q.y) - 0.19f * q.x;
            dq.y = Ccino::Sin(q.z) - 0.19f * q.y;
            dq.z = Ccino::Sin(q.x) - 0.19f * q.z;
        }
        if (mode == 3) {
            scale = 0.16f;
            rate = 0.2f;
            q = q / scale + Ccino::Vec3(-2.95f, -2.95f, -2.95f);
            dq.x = -1.89f * q.x - 4.0f * q.y - 4.0f * q.z - q.y * q.y;
            dq.y = -1.89f * q.y - 4.0f * q.z - 4.0f * q.x - q.z * q.z;
            dq.z = -1.89f * q.z - 4.0f * q.x - 4.0f * q.y - q.x * q.x;
        }
        Ccino::Vec3 v = Ccino::Vec3(dq.x, dq.z, dq.y) * (scale * rate);
        float len = Ccino::Length(v);
        if (len > 6.0f) {
            v = v * (6.0f / len);
        }
        return v;
    }

    // 每帧在 LoopRun 的 onUpdate Hook 中更新, 各 Pass 的 func 在录制时按值捕获进 Shader Lambda
    struct DemoState {
        float time = 0.0f;
        float dt = 0.0f;
        int modeFrom = 0;
        int modeTo = 0;
        float morph = 1.0f;
        float sinceSwitch = 0.0f;
        float hue = kAttractorHues[0];

        float yaw = 0.6f;
        float pitch = 0.35f;
        float distance = 4.6f;
        Ccino::Mat4 viewProj{1.0f};
        Ccino::Vec2 projScale{1.0f};
        Ccino::Vec3 viewDir{0.0f, 0.0f, -1.0f};

        float mouseX = kWidth * 0.5f;
        float mouseY = kHeight * 0.5f;
        float windowWidth = static_cast<float>(kWidth);
        float windowHeight = static_cast<float>(kHeight);
        bool holeActive = false;
        Ccino::Vec4 hole{0.0f};  // xyz 黑洞位置, w 强度
        Ccino::Vec4 shock{0.0f}; // xyz 冲击波中心, w 强度, 随时间衰减
        float exposure = 1.0f;

        bool pendingReset = true; // 第一帧 Buffer 内容未定义, 由 Compute Pass 全部初始化
        bool hdrReady = false;    // 第一帧 HDR 纹理需要 Clear 而不是 Load
    };

    void SwitchAttractor(DemoState& state, int mode) {
        mode = (mode % kAttractorCount + kAttractorCount) % kAttractorCount;
        state.modeFrom = state.morph < 0.5f ? state.modeFrom : state.modeTo;
        state.modeTo = mode;
        state.morph = state.modeFrom == state.modeTo ? 1.0f : 0.0f;
        state.sinceSwitch = 0.0f;
        std::cout << "Attractor: " << kAttractorNames[mode] << std::endl;
    }

    // 鼠标射线与过原点、垂直于视线的平面求交
    Ccino::Vec3 MouseToWorld(const DemoState& state) {
        const float ndcX = 2.0f * state.mouseX / state.windowWidth - 1.0f;
        const float ndcY = 2.0f * state.mouseY / state.windowHeight - 1.0f;
        const Ccino::Mat4 invViewProj = glm::inverse(state.viewProj);
        Ccino::Vec4 nearPoint = invViewProj * Ccino::Vec4(ndcX, ndcY, 0.0f, 1.0f);
        Ccino::Vec4 farPoint = invViewProj * Ccino::Vec4(ndcX, ndcY, 1.0f, 1.0f);
        const Ccino::Vec3 origin = Ccino::Vec3(nearPoint) / nearPoint.w;
        const Ccino::Vec3 dir = glm::normalize(Ccino::Vec3(farPoint) / farPoint.w - origin);
        const float denom = glm::dot(dir, state.viewDir);
        if (std::abs(denom) < 1e-4f) {
            return Ccino::Vec3(0.0f);
        }
        return origin + dir * (-glm::dot(origin, state.viewDir) / denom);
    }

    bool HandleEvent(DemoState& state, const SDL_Event& event) {
        switch (event.type) {
        case SDL_EVENT_KEY_DOWN:
            if (event.key.repeat) {
                break;
            }
            if (event.key.key == SDLK_ESCAPE) {
                return true;
            } else if (event.key.key >= SDLK_1 && event.key.key < SDLK_1 + kAttractorCount) {
                SwitchAttractor(state, static_cast<int>(event.key.key - SDLK_1));
            } else if (event.key.key == SDLK_TAB) {
                SwitchAttractor(state, state.modeTo + 1);
            } else if (event.key.key == SDLK_SPACE) {
                state.shock = Ccino::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
            } else if (event.key.key == SDLK_R) {
                state.pendingReset = true;
            } else if (event.key.key == SDLK_UP || event.key.key == SDLK_DOWN) {
                state.exposure = std::clamp(state.exposure * (event.key.key == SDLK_UP ? 1.2f : 1.0f / 1.2f), 0.1f, 10.0f);
                std::cout << "Exposure: " << state.exposure << std::endl;
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            state.mouseX = event.motion.x;
            state.mouseY = event.motion.y;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            state.mouseX = event.button.x;
            state.mouseY = event.button.y;
            const bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            if (event.button.button == SDL_BUTTON_LEFT) {
                state.holeActive = down;
            } else if (event.button.button == SDL_BUTTON_RIGHT && down) {
                state.shock = Ccino::Vec4(MouseToWorld(state), 1.0f);
            }
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
            state.distance = std::clamp(state.distance * std::pow(0.9f, event.wheel.y), 1.8f, 12.0f);
            break;
        default:
            break;
        }
        return false;
    }

    void UpdateFrame(DemoState& state, float dt) {
        state.dt = dt;
        state.time += dt;

        state.sinceSwitch += dt;
        if (state.sinceSwitch > kAutoSwitchSeconds) {
            SwitchAttractor(state, state.modeTo + 1);
        }
        state.morph = std::min(1.0f, state.morph + dt / kMorphSeconds);
        const float blend = glm::smoothstep(0.0f, 1.0f, state.morph);
        state.hue = glm::mix(kAttractorHues[state.modeFrom], kAttractorHues[state.modeTo], blend) +
                    0.04f * std::sin(state.time * 0.3f);

        // 相机绕原点缓慢旋转, 投影是 Vulkan 的 [0, 1] 深度并翻转 y
        state.yaw += dt * 0.12f;
        const Ccino::Vec3 eye = state.distance * Ccino::Vec3(std::cos(state.pitch) * std::sin(state.yaw), std::sin(state.pitch),
                                                             std::cos(state.pitch) * std::cos(state.yaw));
        const Ccino::Mat4 view = glm::lookAt(eye, Ccino::Vec3(0.0f), Ccino::Vec3(0.0f, 1.0f, 0.0f));
        Ccino::Mat4 proj = glm::perspectiveRH_ZO(glm::radians(50.0f), static_cast<float>(kWidth) / kHeight, 0.05f, 100.0f);
        proj[1][1] *= -1.0f;
        state.viewProj = proj * view;
        state.projScale = Ccino::Vec2(proj[0][0], proj[1][1]);
        state.viewDir = glm::normalize(-eye);

        state.hole = Ccino::Vec4(MouseToWorld(state), state.holeActive ? 1.0f : 0.0f);
        state.shock.w = std::max(0.0f, state.shock.w - dt * 3.0f);
    }

    int RunDemo(uint64_t maxFrames) {
        Ccino::Context ctx;
        RHISettings settings{};
        settings.bUseSwapChain = true;
        settings.flightFrameCount = 2;
        settings.windowWidth = kWidth;
        settings.windowHeight = kHeight;
        settings.bVsync = false;
        settings.bUseImgui = true;
        if (!ctx.Init(settings)) {
            std::cerr << "Failed to initialize Ccino context\n";
            return 1;
        }

        // 三个资源都要跨帧保留内容, 用外部资源由 Demo 持有
        Ccino::Ref<Ccino::Buffer> particleBuffer = ctx.CreateBuffer(Ccino::Buffer::CreateArgs{
            .size = kParticleCount * static_cast<uint32_t>(sizeof(Particle)),
        });
        Ccino::Ref<Ccino::Texture2D> hdrTarget = ctx.CreateTexture2D(Ccino::Texture2D::CreateArgs{
            .width = kWidth,
            .height = kHeight,
            .mipLevels = 1,
            .format = Ccino::TextureFormat::RGBA16_Float,
            .usage = Ccino::TextureUsage::ColorAttachment | Ccino::TextureUsage::Sampled,
        });
        Ccino::Ref<Ccino::Texture2D> presentTarget = ctx.CreateTexture2D(Ccino::Texture2D::CreateArgs{
            .width = kWidth,
            .height = kHeight,
            .mipLevels = 1,
            .format = Ccino::TextureFormat::RGBA8_sRGB,
            .usage = Ccino::TextureUsage::ColorAttachment | Ccino::TextureUsage::TransferSrc,
        });

        Ccino::RGBuilder* builder = ctx.CreateRGBuilder("ParticleGalaxy");
        RID particleRID = builder->CreateExternalBuffer(particleBuffer);
        RID hdrRID = builder->CreateExternalTexture2D(hdrTarget);
        RID presentRID = builder->CreateExternalTexture2D(presentTarget);

        DemoState state;

        builder->AddPass<"ParticleSimulatePass">([&](Ccino::RGBuilder& rg, RID& passRID) {
            auto particles = rg.ReadWrite<Particle>(particleRID, passRID);
            float dt = state.dt;
            float time = state.time;
            int modeFrom = state.modeFrom;
            int modeTo = state.modeTo;
            float morph = glm::smoothstep(0.0f, 1.0f, state.morph);
            int reset = state.pendingReset ? 1 : 0;
            state.pendingReset = false;
            Ccino::Vec4 hole = state.hole;
            Ccino::Vec4 shock = state.shock;
            Ccino::Vec3 viewDir = state.viewDir;
            int count = static_cast<int>(kParticleCount);

            rg.AddCS<Ccino::ThreadGroupSize{kThreadGroupSize, 1, 1}>(
                [particles, dt, time, modeFrom, modeTo, morph, reset, hole, shock, viewDir, count]() {
                    uint32_t id = Ccino::ComputeGlobalID.x;
                    float fid = float(id);
                    Ccino::Vec4 posAge = particles[id].posAge;
                    Ccino::Vec4 driftSeed = particles[id].driftSeed;
                    Ccino::Vec3 p = Ccino::Vec3(posAge.x, posAge.y, posAge.z);
                    Ccino::Vec3 drift = Ccino::Vec3(driftSeed.x, driftSeed.y, driftSeed.z);
                    float age = posAge.w + dt;
                    float seed = driftSeed.w;
                    if (reset != 0) {
                        seed = Hash13(Ccino::Vec3(fid * 0.0137f, 4.31f, 7.17f));
                    }
                    float lifeSpan = 5.0f + 9.0f * seed;

                    // 寿命结束或数值发散时重生: 大部分粒子克隆一个随机粒子的位置并淡入, 吸引子的形态保持锐利;
                    // 少量从中心球内重生, 被速度场卷回吸引子上形成流入的光带. 读别的粒子存在竞争, 读到新旧值都可以
                    bool respawn = reset != 0 || age > lifeSpan || Ccino::IsNan(p.x) || Ccino::IsNan(drift.x) ||
                                   Ccino::Length(p) > 4.0f;
                    if (respawn) {
                        float h1 = Hash13(Ccino::Vec3(fid * 0.0173f, time * 1.37f + 0.11f, seed * 91.7f));
                        float h2 = Hash13(Ccino::Vec3(fid * 0.0119f + 3.7f, time * 0.73f + 5.3f, seed * 37.3f));
                        float h3 = Hash13(Ccino::Vec3(fid * 0.0091f + 9.1f, time * 2.11f + 1.9f, seed * 53.9f));
                        float h4 = Hash13(Ccino::Vec3(fid * 0.0057f + 1.3f, time * 1.91f + 7.7f, seed * 17.1f));
                        float theta = h1 * 6.2831853f;
                        float cz = h2 * 2.0f - 1.0f;
                        float sz = Ccino::Sqrt(Ccino::Max(0.0f, 1.0f - cz * cz));
                        Ccino::Vec3 dir = Ccino::Vec3(sz * Ccino::Cos(theta), cz, sz * Ccino::Sin(theta));
                        int donor = Ccino::Min(int(h4 * float(count)), count - 1);
                        Ccino::Vec4 donorPosAge = particles[donor].posAge;
                        Ccino::Vec3 donorPos = Ccino::Vec3(donorPosAge.x, donorPosAge.y, donorPosAge.z);
                        bool donorValid = !Ccino::IsNan(donorPos.x) && Ccino::Length(donorPos) < 3.0f;
                        if (reset != 0 || h3 < 0.08f || !donorValid) {
                            p = dir * (0.2f + 0.9f * Ccino::Sqrt(h4));
                        } else {
                            p = donorPos + dir * (0.01f * h3);
                        }
                        drift = Ccino::Vec3(0.0f);
                        age = 0.0f;
                        if (reset != 0) {
                            age = h3 * lifeSpan;
                        }
                    }

                    // 切换吸引子时两个速度场插值, 粒子从一种形态流到另一种
                    Ccino::Vec3 flowFrom = AttractorVelocity(modeFrom, p);
                    Ccino::Vec3 flowTo = AttractorVelocity(modeTo, p);
                    Ccino::Vec3 flow = Ccino::Mix(flowFrom, flowTo, morph);

                    if (hole.w > 0.0f) {
                        Ccino::Vec3 toHole = Ccino::Vec3(hole.x, hole.y, hole.z) - p;
                        float dist2 = Ccino::Dot(toHole, toHole) + 0.04f;
                        Ccino::Vec3 swirl = Ccino::Cross(viewDir, toHole);
                        drift = drift + (toHole * 2.0f + swirl * 4.5f) * (hole.w * dt / dist2);
                    }
                    if (shock.w > 0.0f) {
                        Ccino::Vec3 away = p - Ccino::Vec3(shock.x, shock.y, shock.z);
                        float dist = Ccino::Length(away) + 0.001f;
                        drift = drift + away * (shock.w * 40.0f * dt * Ccino::Exp(-dist * 1.3f) / dist);
                    }
                    drift = drift * Ccino::Exp(-dt * 1.4f);

                    Ccino::Vec3 vel = flow + drift;
                    float speed = Ccino::Length(vel);
                    if (speed > 9.0f) {
                        vel = vel * (9.0f / speed);
                        speed = 9.0f;
                    }
                    p = p + vel * dt;

                    particles[id].posAge = Ccino::Vec4(p, age);
                    particles[id].driftSeed = Ccino::Vec4(drift, seed);
                    particles[id].motion = Ccino::Vec4(speed, Ccino::Length(drift), 0.0f, 0.0f);
                },
                Ccino::WorkGroupSize{kParticleCount / kThreadGroupSize, 1, 1});
        }).AddPass<"ParticleFadePass">([&](Ccino::RGBuilder& rg, RID& passRID) {
            // Alpha 混合画黑色: dst = dst * (1 - fade), 按时间衰减和帧率无关
            float fade = 1.0f - std::exp(-state.dt * 7.0f);

            auto vs = []() {
                Ccino::Vec2 pos = Ccino::Vec2(-1.0f, -1.0f);
                if (Ccino::VertexIndex == 1) {
                    pos = Ccino::Vec2(3.0f, -1.0f);
                }
                if (Ccino::VertexIndex == 2) {
                    pos = Ccino::Vec2(-1.0f, 3.0f);
                }
                Ccino::Position = Ccino::Vec4(pos, 0.0f, 1.0f);
            };
            auto fs = [fade](Ccino::Vec4& outColor) {
                outColor = Ccino::Vec4(0.0f, 0.0f, 0.0f, fade);
            };

            auto target = rg.RTView(hdrRID, passRID, state.hdrReady ? Ccino::LoadOp::Load : Ccino::LoadOp::Clear, kBlack);
            state.hdrReady = true;
            rg.AddVSAndFS(vs, fs, Ccino::GraphicsSetting{.drawCounts = 3, .blendMode = Ccino::BlendMode::Alpha}, {target});
        }).AddPass<"ParticleDrawPass">([&](Ccino::RGBuilder& rg, RID& passRID) {
            auto particles = rg.Read<Particle>(particleRID, passRID);
            Ccino::Mat4 viewProj = state.viewProj;
            Ccino::Vec2 projScale = state.projScale;
            float particleSize = 0.0085f;
            // 拖尾每帧衰减, 累积亮度约为 单帧亮度 / fade, 按 dt 缩放后与帧率无关
            float intensity = 0.012f * state.dt * 60.0f;
            float hue = state.hue;

            // 每个粒子 6 个顶点组成面片, 在裁剪空间按视空间尺寸偏移, 不需要顶点缓冲
            auto vs = [particles, viewProj, projScale, particleSize, intensity, hue](Ccino::Vec4& color, Ccino::Vec2& uv) {
                int pid = Ccino::VertexIndex / 6;
                int corner = Ccino::VertexIndex - pid * 6;
                Ccino::Vec2 quad = Ccino::Vec2(-1.0f, -1.0f);
                if (corner == 1 || corner == 4) {
                    quad = Ccino::Vec2(1.0f, -1.0f);
                }
                if (corner == 2 || corner == 3) {
                    quad = Ccino::Vec2(-1.0f, 1.0f);
                }
                if (corner == 5) {
                    quad = Ccino::Vec2(1.0f, 1.0f);
                }

                Ccino::Vec4 posAge = particles[pid].posAge;
                Ccino::Vec4 driftSeed = particles[pid].driftSeed;
                Ccino::Vec4 motion = particles[pid].motion;
                float seed = driftSeed.w;
                float lifeSpan = 5.0f + 9.0f * seed;
                float fadeIn = Ccino::SmoothStep(0.0f, 0.8f, posAge.w);
                float fadeOut = 1.0f - Ccino::SmoothStep(lifeSpan - 1.5f, lifeSpan, posAge.w);
                float heat = Ccino::Clamp(motion.x * 0.45f, 0.0f, 1.0f);
                float burst = Ccino::Clamp(motion.y * 0.4f, 0.0f, 1.0f);

                // 余弦调色板: 色相随吸引子、速度和个体种子变化, 高速粒子偏白热
                Ccino::Vec3 phase = Ccino::Vec3(0.0f, 0.33f, 0.67f) + Ccino::Vec3(hue + heat * 0.45f + seed * 0.12f);
                Ccino::Vec3 rgb = Ccino::Vec3(0.5f) + Ccino::Cos(phase * 6.2831853f) * 0.5f;
                rgb = Ccino::Mix(rgb, Ccino::Vec3(1.0f, 0.85f, 0.7f), heat * heat * 0.6f + burst * 0.4f);
                float brightness = intensity * (0.25f + 1.6f * heat * heat + burst) * fadeIn * fadeOut;
                color = Ccino::Vec4(rgb * brightness, 1.0f);
                uv = quad;

                float size = particleSize * (0.6f + 0.8f * seed) * (1.0f + 0.6f * heat);
                Ccino::Vec4 clip = viewProj * Ccino::Vec4(posAge.x, posAge.y, posAge.z, 1.0f);
                clip.x = clip.x + quad.x * size * projScale.x;
                clip.y = clip.y + quad.y * size * projScale.y;
                Ccino::Position = clip;
            };
            auto fs = [](const Ccino::Vec4& color, const Ccino::Vec2& uv, Ccino::Vec4& outColor) {
                float r2 = Ccino::Dot(uv, uv);
                float mask = Ccino::Max(0.0f, 1.0f - r2);
                float glow = mask * mask * (0.3f + 0.7f * Ccino::Exp(-r2 * 8.0f));
                outColor = color * glow;
            };

            auto target = rg.RTView(hdrRID, passRID, Ccino::LoadOp::Load);
            rg.AddVSAndFS(vs, fs,
                          Ccino::GraphicsSetting{.drawCounts = kParticleCount * 6, .blendMode = Ccino::BlendMode::Additive},
                          {target});
        }).AddPass<"ParticleCompositePass">([&](Ccino::RGBuilder& rg, RID& passRID) {
            auto hdrTex = rg.Sample<Ccino::ViewType::Float>(hdrRID, passRID);
            Ccino::Vec2 texel(1.0f / kWidth, 1.0f / kHeight);
            float exposure = state.exposure;
            float bloomStrength = 0.55f;
            float bloomRadius = 18.0f;

            auto vs = [](Ccino::Vec2& uv) {
                Ccino::Vec2 pos = Ccino::Vec2(-1.0f, -1.0f);
                if (Ccino::VertexIndex == 1) {
                    pos = Ccino::Vec2(3.0f, -1.0f);
                }
                if (Ccino::VertexIndex == 2) {
                    pos = Ccino::Vec2(-1.0f, 3.0f);
                }
                uv = pos * 0.5f + 0.5f;
                Ccino::Position = Ccino::Vec4(pos, 0.0f, 1.0f);
            };
            auto fs = [hdrTex, texel, exposure, bloomStrength, bloomRadius](const Ccino::Vec2& uv, Ccino::Vec4& outColor) {
                Ccino::Vec3 base = Ccino::Vec3(hdrTex.Sample(uv, Ccino::SamplerType::LINEAR_CLAMP_TO_EDGE, 0.0f));

                // 黄金角螺旋上的 24 个采样做大半径柔光, 越外圈权重越小
                Ccino::Vec3 bloom = Ccino::Vec3(0.0f);
                for (int i = 0; i < 24; ++i) {
                    float fi = float(i) + 0.5f;
                    float angle = fi * 2.3999632f;
                    float ring = Ccino::Sqrt(fi / 24.0f);
                    Ccino::Vec2 tap = Ccino::Vec2(Ccino::Cos(angle) * texel.x, Ccino::Sin(angle) * texel.y) * (ring * bloomRadius);
                    float weight = 1.0f - 0.75f * ring;
                    bloom = bloom + Ccino::Vec3(hdrTex.Sample(uv + tap, Ccino::SamplerType::LINEAR_CLAMP_TO_EDGE, 0.0f)) * weight;
                }
                bloom = bloom * (1.0f / 12.0f);

                Ccino::Vec2 centered = uv - Ccino::Vec2(0.5f);
                float vignette = 1.0f - 0.9f * Ccino::Dot(centered, centered);
                Ccino::Vec3 backdrop = Ccino::Vec3(0.006f, 0.008f, 0.02f) * (1.0f - Ccino::Length(centered));
                Ccino::Vec3 hdr = (base + bloom * bloomStrength + backdrop) * exposure;

                // ACES 拟合曲线
                Ccino::Vec3 mapped = (hdr * (hdr * 2.51f + 0.03f)) / (hdr * (hdr * 2.43f + 0.59f) + 0.14f);
                mapped = Ccino::Clamp(mapped * vignette, 0.0f, 1.0f);
                outColor = Ccino::Vec4(mapped, 1.0f);
            };

            auto target = rg.RTView(presentRID, passRID, Ccino::LoadOp::DontCare);
            rg.AddVSAndFS(vs, fs, Ccino::GraphicsSetting{.drawCounts = 3}, {target});
        });

        std::cout << "ParticleGalaxy: " << kParticleCount << " particles\n"
                  << "  LMB hold: black hole | RMB: shockwave | Wheel: zoom\n"
                  << "  1-4 / Tab: switch attractor | Space: burst | Up/Down: exposure | R: reset | Esc: quit" << std::endl;

        const auto start = std::chrono::steady_clock::now();
        auto lastReport = start;
        uint64_t frameCount = 0;
        uint64_t reportFrames = 0;
        builder->LoopRun(presentRID, Ccino::LoopHooks{
            .onEvent = [&](SDL_Event& event) {
                return HandleEvent(state, event);
            },
            .onResize = [&](uint32_t width, uint32_t height) {
                if (width > 0 && height > 0) {
                    state.windowWidth = static_cast<float>(width);
                    state.windowHeight = static_cast<float>(height);
                }
            },
            .onUpdate = [&](Ccino::FrameContext& frame) {
                if (maxFrames != 0 && frame.frameIndex >= maxFrames) {
                    frame.shouldExit = true;
                    return;
                }
                if (frame.width > 0 && frame.height > 0) {
                    state.windowWidth = static_cast<float>(frame.width);
                    state.windowHeight = static_cast<float>(frame.height);
                }
                const float dt = std::clamp(frame.dt, 0.0f, 1.0f / 30.0f);
                UpdateFrame(state, dt);
            },
            .onPostFrame = [&](Ccino::FrameContext& frame) {
                frameCount = frame.frameIndex;
                ++reportFrames;
                const auto now = std::chrono::steady_clock::now();
                const double sinceReport = std::chrono::duration<double>(now - lastReport).count();
                if (sinceReport > 2.0) {
                    std::cout << "FPS: " << reportFrames / sinceReport << std::endl;
                    lastReport = now;
                    reportFrames = 0;
                }
            },
        });

        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::cout << "Rendered " << frameCount << " frames in " << seconds << " s";
        if (seconds > 0.0) {
            std::cout << " (" << frameCount / seconds << " FPS)";
        }
        std::cout << std::endl;

        particleBuffer.Reset();
        hdrTarget.Reset();
        presentTarget.Reset();
        ctx.DestroyRGBuilder("ParticleGalaxy");
        ctx.Destroy();
        return 0;
    }
}

int main(int argc, char** argv) {
    const uint64_t maxFrames = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 0;
    try {
        return RunDemo(maxFrames);
    } catch (const std::exception& e) {
        std::cerr << "ParticleGalaxy failed: " << e.what() << "\n";
        return 1;
    }
}
