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
        if (event.type != SDL_EVENT_MOUSE_MOTION) {
            return;
        }
        constexpr float kSensitivity = 0.002f;
        m_yaw += event.motion.xrel * kSensitivity;
        m_pitch -= event.motion.yrel * kSensitivity;
        const float pitchLimit = glm::radians(89.0f);
        m_pitch = std::clamp(m_pitch, -pitchLimit, pitchLimit);
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
    Ccino::Mat4 m_view{1.0f};
    Ccino::Mat4 m_perspective{1.0f};
    Ccino::Vec3 m_position{};
    Ccino::Vec3 m_lookAtVec{0.0f, 0.0f, 1.0f};
};
