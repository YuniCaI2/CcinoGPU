#include "XPBD.h"
#include "CcinoType.h"


float VolumeConstraint::GetConstraint_GPU(Ccino::Accessor<Particle, BufferAccessFlagBits::Read>& particles, VolumeConstraint vc){
    Ccino::Vec3 p[8];
    Ccino::Vec3 center = Ccino::Vec3(0, 0, 0);

    //八个顶点 (顶面 0-3 俯视逆时针, 底面 4-7 与顶面对应俯视逆时针)
    for (int i = 0; i < 8; i++) {
        p[i] = particles[vc.particles[i]].pos;
        center = center + p[i];
    }
    center = center / 8.0f;

    for (int i = 0; i < 8; i++) {
        p[i] = p[i] - center;
    }

    //四面体求和
    float volume = 0.0f;
    volume += GetVolume_GPU(p[0], p[1], p[2]) + GetVolume_GPU(p[0], p[2], p[3]);
    volume += GetVolume_GPU(p[4], p[7], p[6]) + GetVolume_GPU(p[4], p[6], p[5]);
    volume += GetVolume_GPU(p[1], p[5], p[6]) + GetVolume_GPU(p[1], p[6], p[2]);
    volume += GetVolume_GPU(p[0], p[3], p[7]) + GetVolume_GPU(p[0], p[7], p[4]);
    volume += GetVolume_GPU(p[2], p[6], p[7]) + GetVolume_GPU(p[2], p[7], p[3]);
    volume += GetVolume_GPU(p[0], p[4], p[5]) + GetVolume_GPU(p[0], p[5], p[1]);

    return volume - vc.origin;
}

float VolumeConstraint::GetVolume_GPU(Ccino::Vec3 vec1, Ccino::Vec3 vec2, Ccino::Vec3 vec3) {
    float result = 0;
    result = (vec1.x * vec2.y * vec3.z + vec1.y * vec2.z * vec3.x + vec2.x * vec3.y * vec1.z) - (vec1.z * vec2.y * vec3.x + vec2.z * vec3.y * vec1.x + vec1.y * vec2.x * vec3.z);
    return result / 6.0f;
}

float BounceConstraint::GetConstraint_GPU(Ccino::Accessor<Particle, BufferAccessFlagBits::Read>& particles, BounceConstraint bc) {

}


Ccino::Ref<Ccino::Buffer> XPBDSystem::RegisterObject(const std::vector<CubeVertex>& vertices, const std::vector<CubeIndex>& cubeIndex) {

}
