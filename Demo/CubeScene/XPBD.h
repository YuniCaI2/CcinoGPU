#pragma once
//此代码用来处理XPBD的Pass编排和数据结构设计
#include "Accessor.h"
#include "AccessorEnums.h"
#include "Buffer.h"
#include "CcinoType.h"
#include "Context/CcinoContext.h"
#include "RefCounted.hpp"
#include "RenderGraph.h"
#include "CubeMesh.h"
#include <array>
#include <cstdint>
#include <vector>


//这里的对象的顶点位置会从StaticMesh中获得
struct Particle {
    float w = 1.0f; //这里为 1 / m
    Ccino::Vec3 pos{};
    Ccino::Vec3 prevPos{};
    Ccino::Vec3 vel{};
    Ccino::Vec3 restPos{};
};

struct VolumeConstraint {
    float origin = 1.0f;
    float lambda = 0.0f;
    // 从上到下， 逆时针
    std::array<uint32_t, 8> particles;
    static void GetConstraint_GPU(std::array<Ccino::Vec3, 8> p, float origin, float& outConstraint, std::array<Ccino::Vec3, 8>& outGrad);
    static float GetVolume_GPU(Ccino::Vec3 vec1, Ccino::Vec3 vec2, Ccino::Vec3 vec3);
};


struct BounceConstraint {
    float origin = 1.0f;
    float lambda = 0.0f;
    std::array<uint32_t, 2> particles;
    static void GetConstraint_GPU(Ccino::Vec3 p0, Ccino::Vec3 p1, float origin, float& outConstraint, std::array<Ccino::Vec3, 2>& outGrad);
};

struct ColorBatch {
    uint32_t offset = 0;
    uint32_t count = 0;
};

struct ConstraintBuffer {
    Ccino::Ref<Ccino::Buffer> buffer;
    std::vector<ColorBatch> batches;
};

struct XPBDObject {
    Ccino::Ref<Ccino::Buffer> particles;
    uint32_t particleCount = 0;
    ConstraintBuffer volumeConstraints;
    ConstraintBuffer bounceConstraints;
    RID particleRID = RID::Invalid();
    RID volumeRID = RID::Invalid();
    RID bounceRID = RID::Invalid();
};

class XPBDSystem {
public:
    explicit XPBDSystem(Ccino::Context& context) : m_context(context) {}

    //这里先简单实现1对1 简单映射
    Ccino::Ref<Ccino::Buffer> RegisterObject(const std::vector<CubeVertex>& vertices); //这里应该是一个纯的Pos 来自Static Mesh
    void AddPasses(Ccino::RGBuilder& rgBuilder, const Ccino::FrameContext& frame);
    RID GetParticleRID(size_t index = 0) const { return m_objects[index].particleRID; }
    void RequestReset() { m_pendingReset = true; }
    void Clear() { m_objects.clear(); }

    //约束Pass定义
    void AddPredict(Ccino::RGBuilder& rgBuilder, const Ccino::FrameContext& frame);
    void AddVolumeConstraint(Ccino::RGBuilder& rgBuilder, const Ccino::FrameContext& frame, bool resetLambda = true);
    void AddTerraConstraint(Ccino::RGBuilder& rgBuilder, const Ccino::FrameContext& frame);
    void AddBounceConstraint(Ccino::RGBuilder& rgBuilder, const Ccino::FrameContext& frame, bool resetLambda = true);
    void AddUpdateVelocity(Ccino::RGBuilder& rgBuilder, const Ccino::FrameContext& frame);

    float alpha = 0.00001f; //柔度
    float beta = 0.02f;     //瑞利阻尼
    float height = 1.5f;    //重置下落高度

private:
    void EnsureRegistered(Ccino::RGBuilder& rgBuilder);

    Ccino::Context& m_context;
    std::vector<XPBDObject> m_objects;
    bool m_pendingReset = false;

    inline static constexpr uint32_t kSolverIterations = 8;
    inline static constexpr float kGroundY = -0.5f;
    inline static constexpr uint32_t g_threadSize = 8;
};
