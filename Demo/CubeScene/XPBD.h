#pragma once
//此代码用来处理XPBD的Pass编排和数据结构设计
#include "Accessor.h"
#include "AccessorEnums.h"
#include "Buffer.h"
#include "CcinoType.h"
#include "RefCounted.hpp"
#include "RenderGraph.h"
#include "CubeMesh.h"
#include <array>
#include <cstdint>


//这里的对象的顶点位置会从StaticMesh中获得
struct Particle {
    float m;
    Ccino::Vec3 pos;
};

struct VolumeConstraint {
    float origin = 1;
    // 从上到下， 逆时针
    std::array<uint32_t, 8> particles;
    //这里就是in ? 但是
    static float GetConstraint_GPU(Ccino::Accessor<Particle, BufferAccessFlagBits::Read>& globalParticles, VolumeConstraint vc);
    static float GetVolume_GPU(Ccino::Vec3 vec1, Ccino::Vec3 vec2, Ccino::Vec3 vec3);
};


struct BounceConstraint {
    float origin = 1;
    std::array<uint32_t, 2> particles;
    static float GetConstraint_GPU(Ccino::Accessor<Particle, BufferAccessFlagBits::Read>& globalParticles, BounceConstraint vc);
};

class XPBDSystem {
public:
    Ccino::Ref<Ccino::Buffer> RegisterObject(const std::vector<CubeVertex>& vertices, const std::vector<CubeIndex>& cubeIndex); //这里应该是一个纯的Pos 来自Static Mesh
    //约束Pass定义
    void AddVolumeConstraint(Ccino::RGBuilder& rgBuilder);
    void AddTerraConstraint(Ccino::RGBuilder& rgBuilder);
    void AddBounceConstraint(Ccino::RGBuilder& rgBuilder);

private:

    //这里定义的是质点
    std::vector<Ccino::Ref<Ccino::Buffer>> m_objects;
    std::vector<Ccino::Ref<Ccino::Buffer>> m_volumeConstraints;
    std::vector<Ccino::Ref<Ccino::Buffer>> m_bounceConstraints;

    float beta = 0.02; //阻尼
    float alpha = 0.5; //柔度
};
