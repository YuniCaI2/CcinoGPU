#pragma once
#include "Core/CcinoType.h"
#include "Demo/RayQueryDemo/Material.h"

#include <cstdint>

// 单源 Path Tracing 采样库 (CPU / GPU Shader Lambda 通用)
// 与 Material.h 相同, 所有会被 Shader Lambda 调用的函数必须以 inline 定义在头文件中
namespace PBR {

    // PCG 32-bit 哈希
    inline uint32_t PCGHash(uint32_t seed) {
        uint32_t state = seed * 747796405u + 2891336453u;
        uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        return (word >> 22u) ^ word;
    }

    // 推进 RNG 状态并返回 [0, 1) 均匀随机数
    inline float NextRand(uint32_t& state) {
        state = state * 747796405u + 2891336453u;
        uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        uint32_t result = (word >> 22u) ^ word;
        return float(result) * (1.0f / 4294967296.0f);
    }

    // 无状态随机接口: 返回 [0, 1)
    inline float RandomNum(uint32_t seed) {
        return float(PCGHash(seed)) * (1.0f / 4294967296.0f);
    }

    // 将局部 Y-up 坐标 (y 沿法线 N, x/z 在切平面) 变换到世界空间
    inline Ccino::Vec3 LocalToWorldYUp(Ccino::Vec3 localDir, Ccino::Vec3 N) {
        Ccino::Vec3 up = Ccino::Abs(N.y) < 0.999f
            ? Ccino::Vec3(0.0f, 1.0f, 0.0f)
            : Ccino::Vec3(1.0f, 0.0f, 0.0f);
        Ccino::Vec3 T = Ccino::Normalize(Ccino::Cross(up, N));
        Ccino::Vec3 B = Ccino::Cross(N, T);
        return Ccino::Normalize(T * localDir.x + N * localDir.y + B * localDir.z);
    }

    // 余弦加权半球重要性采样 (局部 Y-up, PDF = cosTheta / pi)
    inline Ccino::Vec3 SampleCosineHemisphere(float u1, float u2) {
        float r = Ccino::Sqrt(Ccino::Clamp(u1, 0.0f, 1.0f));
        float phi = 6.2831853f * u2;
        float y = Ccino::Sqrt(Ccino::Max(0.0f, 1.0f - u1));
        return Ccino::Vec3(r * Ccino::Cos(phi), y, r * Ccino::Sin(phi));
    }

    // 各向同性 GGX 微表面法线 H 重要性采样 (局部 Y-up, p_h(w) = D(h) * cosTheta)
    // cos^2(theta) = (1 - u1) / (u1 * (a^2 - 1) + 1), a = roughness^2
    inline Ccino::Vec3 SampleGGXHalfVector(float u1, float u2, float roughness) {
        float a = roughness * roughness;
        float a2 = a * a;
        float denom = u1 * (a2 - 1.0f) + 1.0f;
        float cos2Theta = (1.0f - u1) / Ccino::Max(denom, 0.000001f);
        float cosTheta = Ccino::Sqrt(Ccino::Clamp(cos2Theta, 0.0f, 1.0f));
        float sinTheta = Ccino::Sqrt(Ccino::Max(0.0f, 1.0f - cos2Theta));
        float phi = 6.2831853f * u2;
        return Ccino::Vec3(sinTheta * Ccino::Cos(phi), cosTheta, sinTheta * Ccino::Sin(phi));
    }

    // 物理一致的精确 Smith-GGX 几何遮蔽函数 (Path Tracing 用, 无直接光 Disney 重映射)
    inline float GeometrySmithGGX(float NdotV, float NdotL, float roughness) {
        float a = roughness * roughness;
        float a2 = a * a;
        float ggxV = (2.0f * NdotV) / Ccino::Max(NdotV + Ccino::Sqrt(a2 + (1.0f - a2) * NdotV * NdotV), 0.000001f);
        float ggxL = (2.0f * NdotL) / Ccino::Max(NdotL + Ccino::Sqrt(a2 + (1.0f - a2) * NdotL * NdotL), 0.000001f);
        return ggxV * ggxL;
    }

    // 路径追踪用程序化天空环境光 (天空渐变 + 更具立体感的太阳高光盘)
    inline Ccino::Vec3 EvalPathTracingSky(Ccino::Vec3 dir) {
        Ccino::Vec3 groundCol(0.07f, 0.07f, 0.09f);
        Ccino::Vec3 horizonCol(0.32f, 0.37f, 0.46f);
        Ccino::Vec3 zenithCol(0.16f, 0.30f, 0.58f);
        Ccino::Vec3 sky = Ccino::Mix(horizonCol, zenithCol, Ccino::Clamp(dir.y, 0.0f, 1.0f));
        Ccino::Vec3 env = dir.y >= 0.0f
            ? sky
            : Ccino::Mix(horizonCol, groundCol, Ccino::Clamp(-dir.y * 2.5f, 0.0f, 1.0f));

        // 主太阳方向 (与原 keyLightDir 一致)
        Ccino::Vec3 sunDir = Ccino::Normalize(Ccino::Vec3(0.55f, 0.72f, -0.42f));
        float sunDot = Ccino::Max(Ccino::Dot(dir, sunDir), 0.0f);
        float s2 = sunDot * sunDot;
        float s4 = s2 * s2;
        float s8 = s4 * s4;
        float s16 = s8 * s8;
        float s64 = s16 * s16 * s16 * s16;

        // 柔和的太阳光晕 + 较亮太阳盘, 使纯 Path Tracing 产生清晰的柔和阴影与金属高光
        Ccino::Vec3 sunGlow = Ccino::Vec3(2.4f, 2.1f, 1.7f) * s16;
        Ccino::Vec3 sunDisc = Ccino::Vec3(14.0f, 12.8f, 11.0f) * s64;

        // 冷色边缘补光天光 (与原 fillLightDir 方向呼应)
        Ccino::Vec3 fillDir = Ccino::Normalize(Ccino::Vec3(-0.75f, 0.40f, -0.52f));
        float fillDot = Ccino::Max(Ccino::Dot(dir, fillDir), 0.0f);
        float f4 = fillDot * fillDot * fillDot * fillDot;
        Ccino::Vec3 fillGlow = Ccino::Vec3(0.75f, 0.95f, 1.35f) * f4;

        return env + sunGlow + sunDisc + fillGlow;
    }

    struct BSDFSample {
        Ccino::Vec3 dir;    // 采样出的下一跳入射方向 L (世界空间)
        Ccino::Vec3 weight; // 蒙特卡罗权重 f_r * (N dot L) / pdf
        int valid;          // 1 有效, 0 终止
    };

    // PBR 金属度-粗糙度 BSDF 重要性采样 (随机选择 GGX Specular 或 余弦 Diffuse 分支)
    inline BSDFSample SamplePBRBSDF(Surface s, Ccino::Vec3 V, uint32_t& rngState) {
        BSDFSample outSample;
        outSample.dir = Ccino::Vec3(0.0f, 1.0f, 0.0f);
        outSample.weight = Ccino::Vec3(0.0f, 0.0f, 0.0f);
        outSample.valid = 0;

        Ccino::Vec3 N = s.normal;
        float NdotV = Ccino::Dot(N, V);
        if (NdotV <= 0.0001f) {
            return outSample;
        }

        Ccino::Vec3 F0 = Ccino::Mix(Ccino::Vec3(0.04f, 0.04f, 0.04f), s.albedo, s.metallic);
        Ccino::Vec3 Fview = FresnelSchlick(NdotV, F0);
        Ccino::Vec3 diffAlbedo = s.albedo * (1.0f - s.metallic);

        // 按能量占比估算 Specular 与 Diffuse 的选择概率
        float specLum = Ccino::Dot(Fview, Ccino::Vec3(0.2126f, 0.7152f, 0.0722f));
        float diffLum = Ccino::Dot(diffAlbedo * (Ccino::Vec3(1.0f, 1.0f, 1.0f) - Fview), Ccino::Vec3(0.2126f, 0.7152f, 0.0722f));
        float pSpec = 1.0f;
        if (s.metallic < 0.999f) {
            float totalLum = specLum + diffLum;
            pSpec = totalLum > 0.0001f ? Ccino::Clamp(specLum / totalLum, 0.05f, 0.95f) : 0.5f;
        }

        float uSelect = NextRand(rngState);
        float u1 = NextRand(rngState);
        float u2 = NextRand(rngState);

        if (uSelect < pSpec) {
            // 1. GGX 镜面反射重要性采样
            Ccino::Vec3 Hlocal = SampleGGXHalfVector(u1, u2, s.roughness);
            Ccino::Vec3 H = LocalToWorldYUp(Hlocal, N);
            Ccino::Vec3 L = Ccino::Normalize(Ccino::Reflect(-V, H));

            float NdotL = Ccino::Dot(N, L);
            float NdotH = Ccino::Dot(N, H);
            float VdotH = Ccino::Dot(V, H);
            if (NdotL <= 0.0001f || NdotH <= 0.0001f || VdotH <= 0.0001f) {
                return outSample;
            }

            Ccino::Vec3 F = FresnelSchlick(VdotH, F0);
            float G = GeometrySmithGGX(NdotV, NdotL, s.roughness);

            // (D * F * G / (4 * NdotV * NdotL)) * NdotL / (pSpec * D * NdotH / (4 * VdotH))
            // = F * G * VdotH / (NdotV * NdotH * pSpec)
            outSample.dir = L;
            outSample.weight = F * (G * VdotH / Ccino::Max(NdotV * NdotH * pSpec, 0.000001f));
            outSample.valid = 1;
            return outSample;
        } else {
            // 2. Lambert 漫反射余弦加权半球采样
            Ccino::Vec3 Llocal = SampleCosineHemisphere(u1, u2);
            Ccino::Vec3 L = LocalToWorldYUp(Llocal, N);
            float NdotL = Ccino::Dot(N, L);
            if (NdotL <= 0.0001f) {
                return outSample;
            }

            Ccino::Vec3 H = Ccino::Normalize(V + L);
            float VdotH = Ccino::Max(Ccino::Dot(V, H), 0.0f);
            Ccino::Vec3 F = FresnelSchlick(VdotH, F0);
            Ccino::Vec3 kD = (Ccino::Vec3(1.0f, 1.0f, 1.0f) - F) * (1.0f - s.metallic);

            // (kD * albedo / pi) * NdotL / ((1 - pSpec) * NdotL / pi)
            // = kD * albedo / (1 - pSpec)
            outSample.dir = L;
            outSample.weight = kD * s.albedo / Ccino::Max(1.0f - pSpec, 0.000001f);
            outSample.valid = 1;
            return outSample;
        }
    }

} // namespace PBR
