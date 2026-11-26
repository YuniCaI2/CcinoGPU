#pragma once
#include "Context/CcinoContext.h"
#include "Core/CcinoType.h"
#include "Core/Texture2D.h"

#include <array>
#include <cstdint>

// 单源 PBR 表面属性与 Cook-Torrance 着色库 (CPU / GPU Shader Lambda 通用)
// 注意: CodeGenerator 按单个编译单元解析 AST (functionDecl->getDefinition()),
// 因此会被 Shader Lambda 调用的着色函数必须以 inline 形式定义在头文件中;
// 仅在 CPU 侧运行的资源辅助函数则放在 Material.cpp 中实现。
namespace PBR {

    struct Surface {
        Ccino::Vec3 normal;
        Ccino::Vec3 albedo;
        float roughness;
        float metallic;
        float ao;
        Ccino::Vec3 emissive;
    };

    inline Ccino::Vec3 FresnelSchlick(float cosTheta, Ccino::Vec3 F0) {
        float x = Ccino::Clamp(1.0f - cosTheta, 0.0f, 1.0f);
        float x2 = x * x;
        float x5 = x2 * x2 * x;
        return F0 + (Ccino::Vec3(1.0f, 1.0f, 1.0f) - F0) * x5;
    }

    inline Ccino::Vec3 FresnelSchlickRoughness(float cosTheta, Ccino::Vec3 F0, float roughness) {
        float x = Ccino::Clamp(1.0f - cosTheta, 0.0f, 1.0f);
        float x2 = x * x;
        float x5 = x2 * x2 * x;
        float oneMinusR = 1.0f - roughness;
        Ccino::Vec3 maxF = Ccino::Max(Ccino::Vec3(oneMinusR, oneMinusR, oneMinusR), F0);
        return F0 + (maxF - F0) * x5;
    }

    // Trowbridge-Reitz GGX 法线分布函数 D
    inline float DistributionGGX(float NdotH, float roughness) {
        float pi = 3.14159265f;
        float a = roughness * roughness;
        float a2 = a * a;
        float denom = (NdotH * NdotH) * (a2 - 1.0f) + 1.0f;
        return a2 / Ccino::Max(pi * denom * denom, 0.000001f);
    }

    // Schlick-GGX 单向几何遮蔽项 G1
    inline float GeometrySchlickGGX(float NdotX, float k) {
        return NdotX / (NdotX * (1.0f - k) + k);
    }

    // Smith 双向几何遮蔽函数 G
    inline float GeometrySmith(float NdotV, float NdotL, float roughness) {
        float rPlus1 = roughness + 1.0f;
        float k = (rPlus1 * rPlus1) * 0.125f;
        return GeometrySchlickGGX(NdotV, k) * GeometrySchlickGGX(NdotL, k);
    }

    // Cook-Torrance 微表面 PBR 直接光 BRDF
    inline Ccino::Vec3 EvalDirectCookTorrance(
        Surface s,
        Ccino::Vec3 V,
        Ccino::Vec3 L,
        Ccino::Vec3 lightRadiance)
    {
        float NdotL = Ccino::Max(Ccino::Dot(s.normal, L), 0.0f);
        if (NdotL <= 0.0f) {
            return Ccino::Vec3(0.0f, 0.0f, 0.0f);
        }

        float NdotV = Ccino::Max(Ccino::Dot(s.normal, V), 0.0001f);
        Ccino::Vec3 H = Ccino::Normalize(V + L);
        float NdotH = Ccino::Max(Ccino::Dot(s.normal, H), 0.0f);
        float HdotV = Ccino::Max(Ccino::Dot(H, V), 0.0f);

        float pi = 3.14159265f;
        Ccino::Vec3 F0 = Ccino::Mix(Ccino::Vec3(0.04f, 0.04f, 0.04f), s.albedo, s.metallic);

        Ccino::Vec3 F = FresnelSchlick(HdotV, F0);
        float D = DistributionGGX(NdotH, s.roughness);
        float G = GeometrySmith(NdotV, NdotL, s.roughness);

        Ccino::Vec3 spec = (F * (D * G)) / Ccino::Max(4.0f * NdotV * NdotL, 0.0001f);
        Ccino::Vec3 kD = (Ccino::Vec3(1.0f, 1.0f, 1.0f) - F) * (1.0f - s.metallic);
        Ccino::Vec3 diff = kD * (s.albedo / pi);

        return (diff + spec) * lightRadiance * NdotL;
    }

    // 程序化环境光 (天空渐变 + 太阳高光圆盘, 同时用于背景和金属环境反射)
    inline Ccino::Vec3 EvalEnvColor(Ccino::Vec3 dir) {
        Ccino::Vec3 groundCol(0.08f, 0.08f, 0.10f);
        Ccino::Vec3 horizonCol(0.30f, 0.35f, 0.44f);
        Ccino::Vec3 zenithCol(0.14f, 0.28f, 0.56f);
        Ccino::Vec3 sky = Ccino::Mix(horizonCol, zenithCol, Ccino::Clamp(dir.y, 0.0f, 1.0f));
        Ccino::Vec3 env = dir.y >= 0.0f
            ? sky
            : Ccino::Mix(horizonCol, groundCol, Ccino::Clamp(-dir.y * 2.5f, 0.0f, 1.0f));

        Ccino::Vec3 sunDir = Ccino::Normalize(Ccino::Vec3(0.55f, 0.72f, -0.42f));
        float sunDot = Ccino::Max(Ccino::Dot(dir, sunDir), 0.0f);
        float s2 = sunDot * sunDot;
        float s4 = s2 * s2;
        float s8 = s4 * s4;
        float s16 = s8 * s8;
        return env + Ccino::Vec3(2.2f, 1.95f, 1.6f) * s16;
    }

    // 简化环境光 IBL (漫反射半球 + 镜面环境反射)
    inline Ccino::Vec3 EvalAmbientIBL(Surface s, Ccino::Vec3 V) {
        float NdotV = Ccino::Max(Ccino::Dot(s.normal, V), 0.0001f);
        Ccino::Vec3 F0 = Ccino::Mix(Ccino::Vec3(0.04f, 0.04f, 0.04f), s.albedo, s.metallic);
        Ccino::Vec3 Frough = FresnelSchlickRoughness(NdotV, F0, s.roughness);
        Ccino::Vec3 kD = (Ccino::Vec3(1.0f, 1.0f, 1.0f) - Frough) * (1.0f - s.metallic);

        Ccino::Vec3 R = Ccino::Reflect(-V, s.normal);
        Ccino::Vec3 envSpec = Ccino::Mix(EvalEnvColor(R), Ccino::Vec3(0.24f, 0.28f, 0.36f), s.roughness * s.roughness);
        Ccino::Vec3 envDiff = Ccino::Mix(
            Ccino::Vec3(0.09f, 0.09f, 0.11f),
            Ccino::Vec3(0.24f, 0.30f, 0.42f),
            Ccino::Clamp(s.normal.y * 0.5f + 0.5f, 0.0f, 1.0f));
        return (kD * s.albedo * envDiff + Frough * envSpec * (1.0f - s.roughness * 0.45f)) * s.ao;
    }

    // 用三角形世界空间边与 UV 差解析构造 TBN 并应用切线空间法线贴图
    inline Ccino::Vec3 ApplyTangentNormalMap(
        Ccino::Vec3 Ninterp,
        Ccino::Vec3 V,
        Ccino::Vec3 e1,
        Ccino::Vec3 e2,
        Ccino::Vec2 duv1,
        Ccino::Vec2 duv2,
        Ccino::Vec3 normalMapSample)
    {
        Ccino::Vec3 N = Ninterp;
        float det = duv1.x * duv2.y - duv2.x * duv1.y;
        if (Ccino::Abs(det) > 0.0000001f) {
            float invDet = 1.0f / det;
            Ccino::Vec3 Traw = (e1 * duv2.y - e2 * duv1.y) * invDet;
            Ccino::Vec3 Tortho = Traw - Ninterp * Ccino::Dot(Ninterp, Traw);
            if (Ccino::Dot(Tortho, Tortho) > 0.000001f) {
                Ccino::Vec3 T = Ccino::Normalize(Tortho);
                Ccino::Vec3 B = Ccino::Cross(Ninterp, T) * (det < 0.0f ? -1.0f : 1.0f);
                Ccino::Vec3 mapN = normalMapSample * 2.0f - Ccino::Vec3(1.0f, 1.0f, 1.0f);
                N = Ccino::Normalize(T * mapN.x + B * mapN.y + Ninterp * mapN.z);
            }
        }
        if (Ccino::Dot(N, V) < 0.0f) {
            N = -N;
        }
        return N;
    }

    // ACES Filmic 色调映射 (线性 HDR -> [0, 1] 线性 LDR, 交由 RGBA8_sRGB 硬件编码)
    inline Ccino::Vec3 ToneMapACES(Ccino::Vec3 hdrColor) {
        Ccino::Vec3 num = hdrColor * (hdrColor * 2.51f + Ccino::Vec3(0.03f, 0.03f, 0.03f));
        Ccino::Vec3 den = hdrColor * (hdrColor * 2.43f + Ccino::Vec3(0.59f, 0.59f, 0.59f)) + Ccino::Vec3(0.14f, 0.14f, 0.14f);
        return Ccino::Clamp(num / den, Ccino::Vec3(0.0f, 0.0f, 0.0f), Ccino::Vec3(1.0f, 1.0f, 1.0f));
    }

    // CPU 侧辅助: 创建 1x1 兜底纯色纹理 (实现在 Material.cpp)
    Ccino::Ref<Ccino::Texture2D> CreateSolidTexture2D(
        Ccino::Context& context,
        std::array<uint8_t, 4> rgba,
        Ccino::TextureFormat format);

} // namespace PBR
