#pragma once
#include "Core/CcinoType.h"

#include <SDL3/SDL_events.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

// yaw / pitch 为 0 时视线沿 +Z. 鼠标水平移动改变 yaw, 垂直移动改变 pitch
class Camera {
public:
    // fov 为角度
    Camera(Ccino::Vec3 position, float fov, float aspect)
        : m_fov(fov), m_aspect(aspect), m_position(position) {
        RebuildProjection();
        RebuildView();
    }

    void Update(const SDL_Event& event) {
        if (event.type == SDL_EVENT_MOUSE_MOTION) {
            constexpr float kSensitivity = 0.002f;
            m_yaw += event.motion.xrel * kSensitivity;
            m_pitch -= event.motion.yrel * kSensitivity;
            const float pitchLimit = glm::radians(89.0f);
            m_pitch = std::clamp(m_pitch, -pitchLimit, pitchLimit);
            RebuildView();
        } else if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
            const bool pressed = event.type == SDL_EVENT_KEY_DOWN;
            switch (event.key.scancode) {
            case SDL_SCANCODE_W: m_moveForward = pressed; break;
            case SDL_SCANCODE_S: m_moveBackward = pressed; break;
            case SDL_SCANCODE_A: m_moveLeft = pressed; break;
            case SDL_SCANCODE_D: m_moveRight = pressed; break;
            default: break;
            }
        } else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
            // 切出窗口时可能收不到 KEY_UP, 清除状态避免持续移动
            m_moveForward = m_moveBackward = m_moveLeft = m_moveRight = false;
        }
    }

    // 按键事件只记录状态, 每帧根据 dt 移动, 不依赖系统的按键重复频率
    void UpdateMovement(float dt) {
        if (dt <= 0.0f) {
            return;
        }

        const float forward = static_cast<float>(m_moveForward) - static_cast<float>(m_moveBackward);
        const float strafe = static_cast<float>(m_moveRight) - static_cast<float>(m_moveLeft);
        if (forward == 0.0f && strafe == 0.0f) {
            return;
        }

        // 右手相机默认朝 +Z, 此时屏幕右方为世界 -X
        const Ccino::Vec3 right = glm::normalize(glm::cross(m_lookAtVec, Ccino::Vec3(0.0f, 1.0f, 0.0f)));
        const Ccino::Vec3 direction = glm::normalize(m_lookAtVec * forward + right * strafe);
        constexpr float kMoveSpeed = 2.5f; // 世界单位 / 秒
        m_position += direction * kMoveSpeed * dt;
        RebuildView();
    }

    void SetAspect(float aspect) {
        m_aspect = aspect;
        RebuildProjection();
    }

    const Ccino::Mat4& View() const { return m_view; }
    const Ccino::Mat4& Perspective() const { return m_perspective; }
    Ccino::Mat4 ViewProj() const { return m_perspective * m_view; }
    const Ccino::Vec3& Position() const { return m_position; }
    const Ccino::Vec3& LookAt() const { return m_lookAtVec; }

private:
    void RebuildProjection() {
        m_perspective = glm::perspectiveRH_ZO(glm::radians(m_fov), m_aspect, 0.01f, 100.0f);
        m_perspective[1][1] *= -1.0f; // Vulkan 裁剪空间 y 向下, 深度 [0, 1]
    }

    void RebuildView() {
        const float cp = std::cos(m_pitch);
        const float sp = std::sin(m_pitch);
        const float cy = std::cos(m_yaw);
        const float sy = std::sin(m_yaw);
        // 正 yaw 转向相机右侧, 正 pitch 抬头
        m_lookAtVec = Ccino::Vec3(-sy * cp, sp, cy * cp);
        m_view = glm::lookAt(m_position, m_position + m_lookAtVec, Ccino::Vec3(0.0f, 1.0f, 0.0f));
    }

    float m_fov = 90.0f;
    float m_aspect = 1.0f;
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;
    bool m_moveForward = false;
    bool m_moveBackward = false;
    bool m_moveLeft = false;
    bool m_moveRight = false;
    Ccino::Mat4 m_view{1.0f};
    Ccino::Mat4 m_perspective{1.0f};
    Ccino::Vec3 m_position{};
    Ccino::Vec3 m_lookAtVec{0.0f, 0.0f, 1.0f};
};
