#include "XPBD.h"
#include "CcinoType.h"
#include "RenderGraph.h"
#include "ShaderStruct/ShaderCommon.h"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <unordered_set>

template <typename Constraint>
static std::vector<ColorBatch> ColorConstraints(uint32_t particleCount, std::vector<Constraint>& constraints) {
    std::vector<uint64_t> vertexMask(particleCount, 0);
    std::vector<std::vector<Constraint>> buckets;

    for (const auto& c : constraints) {
        uint64_t used = 0;
        for (uint32_t v : c.particles) {
            used |= vertexMask[v];
        }
        assert(~used != 0 && "Color count exceeds 64");
        uint32_t color = static_cast<uint32_t>(std::countr_zero(~used));
        uint64_t bit = 1ULL << color;
        for (uint32_t v : c.particles) {
            vertexMask[v] |= bit;
        }
        if (color >= buckets.size()) {
            buckets.resize(color + 1);
        }
        buckets[color].push_back(c);
    }

    constraints.clear();
    std::vector<ColorBatch> batches;
    batches.reserve(buckets.size());
    for (const auto& bucket : buckets) {
        if (bucket.empty()) continue;
        batches.push_back(ColorBatch{
            static_cast<uint32_t>(constraints.size()),
            static_cast<uint32_t>(bucket.size()),
        });
        constraints.insert(constraints.end(), bucket.begin(), bucket.end());
    }
    return batches;
}


static float AccumulateTriVolumeGrad(std::array<Ccino::Vec3, 8> p, int a, int b, int c, std::array<Ccino::Vec3, 8>& outGrad) {
    Ccino::Vec3 bc = Ccino::Cross(p[b], p[c]);
    outGrad[a] = outGrad[a] + bc;
    outGrad[b] = outGrad[b] + Ccino::Cross(p[c], p[a]);
    outGrad[c] = outGrad[c] + Ccino::Cross(p[a], p[b]);
    return Ccino::Dot(p[a], bc);
}

void VolumeConstraint::GetConstraint_GPU(
    std::array<Ccino::Vec3, 8> p,
    float origin,
    float& outConstraint,
    std::array<Ccino::Vec3, 8>& outGrad)
{
    Ccino::Vec3 center = Ccino::Vec3(0.0f, 0.0f, 0.0f);

    //八个顶点 (顶面 0-3 俯视逆时针, 底面 4-7 与顶面对应俯视逆时针)
    for (int i = 0; i < 8; i++) {
        center = center + p[i];
        outGrad[i] = Ccino::Vec3(0.0f, 0.0f, 0.0f);
    }
    center = center / 8.0f;

    for (int i = 0; i < 8; i++) {
        p[i] = p[i] - center;
    }

    //体积与梯度同时累加 (12 个外表面逆时针三角形)
    float volume = 0.0f;
    volume += AccumulateTriVolumeGrad(p, 0, 1, 2, outGrad) + AccumulateTriVolumeGrad(p, 0, 2, 3, outGrad);
    volume += AccumulateTriVolumeGrad(p, 4, 7, 6, outGrad) + AccumulateTriVolumeGrad(p, 4, 6, 5, outGrad);
    volume += AccumulateTriVolumeGrad(p, 1, 5, 6, outGrad) + AccumulateTriVolumeGrad(p, 1, 6, 2, outGrad);
    volume += AccumulateTriVolumeGrad(p, 0, 3, 7, outGrad) + AccumulateTriVolumeGrad(p, 0, 7, 4, outGrad);
    volume += AccumulateTriVolumeGrad(p, 2, 6, 7, outGrad) + AccumulateTriVolumeGrad(p, 2, 7, 3, outGrad);
    volume += AccumulateTriVolumeGrad(p, 0, 4, 5, outGrad) + AccumulateTriVolumeGrad(p, 0, 5, 1, outGrad);

    for (int i = 0; i < 8; i++) {
        outGrad[i] = outGrad[i] / 6.0f;
    }
    outConstraint = volume / 6.0f - origin;
}

float VolumeConstraint::GetVolume_GPU(Ccino::Vec3 vec1, Ccino::Vec3 vec2, Ccino::Vec3 vec3) {
    float result = 0;
    result = (vec1.x * vec2.y * vec3.z + vec1.y * vec2.z * vec3.x + vec2.x * vec3.y * vec1.z) - (vec1.z * vec2.y * vec3.x + vec2.z * vec3.y * vec1.x + vec1.y * vec2.x * vec3.z);
    return result / 6.0f;
}

void BounceConstraint::GetConstraint_GPU(
    Ccino::Vec3 p0,
    Ccino::Vec3 p1,
    float origin,
    float& outConstraint,
    std::array<Ccino::Vec3, 2>& outGrad)
{
    Ccino::Vec3 d = p0 - p1;
    float len = Ccino::Length(d);
    Ccino::Vec3 n = Ccino::Vec3(0.0f, 0.0f, 0.0f);
    if (len > 1e-6f) {
        n = d / len;
    }
    outConstraint = len - origin;
    outGrad[0] = n;
    outGrad[1] = -n;
}


Ccino::Ref<Ccino::Buffer> XPBDSystem::RegisterObject(const std::vector<CubeVertex>& vertices) {
    if (vertices.empty()) {
        return {};
    }

    std::vector<Particle> particles;
    particles.reserve(vertices.size());
    for (const auto& v : vertices) {
        Ccino::Vec3 initPos = v.position + Ccino::Vec3(0.0f, height, 0.0f);
        particles.push_back(Particle{
            .w = 1.0f,
            .pos = initPos,
            .prevPos = initPos,
            .vel = Ccino::Vec3(0.0f),
            .restPos = v.position,
        });
    }

    std::vector<VolumeConstraint> volumeConstraints;
    std::vector<BounceConstraint> bounceConstraints;
    std::unordered_set<uint64_t> edgeSet;

    auto addEdge = [&](uint32_t a, uint32_t b) {
        if (a == b || a >= vertices.size() || b >= vertices.size()) {
            return;
        }
        uint32_t lo = std::min(a, b);
        uint32_t hi = std::max(a, b);
        uint64_t key = (static_cast<uint64_t>(lo) << 32) | static_cast<uint64_t>(hi);
        if (edgeSet.insert(key).second) {
            BounceConstraint bc{};
            bc.origin = Ccino::Length(vertices[lo].position - vertices[hi].position);
            bc.particles = {lo, hi};
            bounceConstraints.push_back(bc);
        }
    };

    const uint32_t res = static_cast<uint32_t>(std::lround(std::cbrt(static_cast<double>(vertices.size())))); //计算立方根四舍五入
    if (res >= 2 && static_cast<size_t>(res) * res * res == vertices.size()) {
        auto gridIdx = [res](uint32_t x, uint32_t y, uint32_t z) -> uint32_t {
            return x + res * (y + res * z);
        };

        const uint32_t cells = res - 1;
        volumeConstraints.reserve(static_cast<size_t>(cells) * cells * cells);

        for (uint32_t z = 0; z < cells; ++z) {
            for (uint32_t y = 0; y < cells; ++y) {
                for (uint32_t x = 0; x < cells; ++x) {
                    VolumeConstraint vc{};
                    vc.particles = {
                        gridIdx(x,     y + 1, z    ),
                        gridIdx(x,     y + 1, z + 1),
                        gridIdx(x + 1, y + 1, z + 1),
                        gridIdx(x + 1, y + 1, z    ),
                        gridIdx(x,     y,     z    ),
                        gridIdx(x,     y,     z + 1),
                        gridIdx(x + 1, y,     z + 1),
                        gridIdx(x + 1, y,     z    ),
                    };

                    Ccino::Vec3 p[8];
                    Ccino::Vec3 center(0.0f);
                    for (int i = 0; i < 8; ++i) {
                        p[i] = vertices[vc.particles[i]].position;
                        center += p[i];
                    }
                    center /= 8.0f;
                    for (int i = 0; i < 8; ++i) {
                        p[i] -= center;
                    }

                    float volume = 0.0f;
                    volume += VolumeConstraint::GetVolume_GPU(p[0], p[1], p[2]) + VolumeConstraint::GetVolume_GPU(p[0], p[2], p[3]);
                    volume += VolumeConstraint::GetVolume_GPU(p[4], p[7], p[6]) + VolumeConstraint::GetVolume_GPU(p[4], p[6], p[5]);
                    volume += VolumeConstraint::GetVolume_GPU(p[1], p[5], p[6]) + VolumeConstraint::GetVolume_GPU(p[1], p[6], p[2]);
                    volume += VolumeConstraint::GetVolume_GPU(p[0], p[3], p[7]) + VolumeConstraint::GetVolume_GPU(p[0], p[7], p[4]);
                    volume += VolumeConstraint::GetVolume_GPU(p[2], p[6], p[7]) + VolumeConstraint::GetVolume_GPU(p[2], p[7], p[3]);
                    volume += VolumeConstraint::GetVolume_GPU(p[0], p[4], p[5]) + VolumeConstraint::GetVolume_GPU(p[0], p[5], p[1]);
                    vc.origin = volume;
                    volumeConstraints.push_back(vc);

                    const auto& id = vc.particles;
                    // 结构边 (12条)
                    addEdge(id[0], id[1]); addEdge(id[1], id[2]); addEdge(id[2], id[3]); addEdge(id[3], id[0]);
                    addEdge(id[4], id[5]); addEdge(id[5], id[6]); addEdge(id[6], id[7]); addEdge(id[7], id[4]);
                    addEdge(id[0], id[4]); addEdge(id[1], id[5]); addEdge(id[2], id[6]); addEdge(id[3], id[7]);
                    // 面对角线 (12条, 剪切弹簧)
                    addEdge(id[0], id[2]); addEdge(id[1], id[3]);
                    addEdge(id[4], id[6]); addEdge(id[5], id[7]);
                    addEdge(id[1], id[6]); addEdge(id[5], id[2]);
                    addEdge(id[0], id[7]); addEdge(id[3], id[4]);
                    addEdge(id[2], id[7]); addEdge(id[6], id[3]);
                    addEdge(id[0], id[5]); addEdge(id[4], id[1]);
                }
            }
        }
    }

    auto createAndUpload = [&](const auto& vec) -> Ccino::Ref<Ccino::Buffer> {
        if (vec.empty()) {
            return {};
        }
        Ccino::Ref<Ccino::Buffer> buffer = m_context.CreateBuffer(Ccino::Buffer::CreateArgs{
            .size = static_cast<uint32_t>(vec.size() * sizeof(vec[0])),
        });
        buffer->Copy(vec.data());
        buffer->Flush();
        return buffer;
    };

    XPBDObject obj{};
    obj.particles = createAndUpload(particles);
    obj.particleCount = static_cast<uint32_t>(particles.size());

    if (!volumeConstraints.empty()) {
        obj.volumeConstraints.batches = ColorConstraints(obj.particleCount, volumeConstraints);
        obj.volumeConstraints.buffer = createAndUpload(volumeConstraints);
    }
    if (!bounceConstraints.empty()) {
        obj.bounceConstraints.batches = ColorConstraints(obj.particleCount, bounceConstraints);
        obj.bounceConstraints.buffer = createAndUpload(bounceConstraints);
    }

    Ccino::Ref<Ccino::Buffer> particleBuffer = obj.particles;
    m_objects.push_back(std::move(obj));
    return particleBuffer;
}

void XPBDSystem::EnsureRegistered(Ccino::RGBuilder& rgBuilder) {
    for (auto& obj : m_objects) {
        if (obj.particleRID == RID::Invalid() && obj.particles.IsValid()) {
            obj.particleRID = rgBuilder.CreateExternalBuffer(obj.particles);
        }
        if (obj.volumeRID == RID::Invalid() && obj.volumeConstraints.buffer.IsValid()) {
            obj.volumeRID = rgBuilder.CreateExternalBuffer(obj.volumeConstraints.buffer);
        }
        if (obj.bounceRID == RID::Invalid() && obj.bounceConstraints.buffer.IsValid()) {
            obj.bounceRID = rgBuilder.CreateExternalBuffer(obj.bounceConstraints.buffer);
        }
    }
}

void XPBDSystem::AddPasses(Ccino::RGBuilder& rgBuilder, const Ccino::FrameContext& frame) {
    EnsureRegistered(rgBuilder);
    AddPredict(rgBuilder, frame);
    for (uint32_t epoch = 0; epoch < kSolverIterations; ++epoch) {
        AddBounceConstraint(rgBuilder, frame, epoch == 0);
        AddVolumeConstraint(rgBuilder, frame, epoch == 0);
    }
    AddTerraConstraint(rgBuilder, frame);
    AddUpdateVelocity(rgBuilder, frame);
}

void XPBDSystem::AddPredict(Ccino::RGBuilder& rgBuilder, const Ccino::FrameContext& frame) {
    EnsureRegistered(rgBuilder);
    for (const auto& obj : m_objects) {
        RID particleRID = obj.particleRID;
        uint32_t count = obj.particleCount;
        rgBuilder.AddPass<"PredictParticles">([this, &frame, particleRID, count](Ccino::RGBuilder& rg, RID& pass) mutable {
            auto particles = rg.ReadWrite<Particle>(particleRID, pass);
            float dt = frame.dt > 0.0f ? frame.dt : (1.0f / 60.0f);
            float resetHeight = height;
            int reset = m_pendingReset ? 1 : 0;
            m_pendingReset = false;

            rg.AddCS<Ccino::ThreadGroupSize{g_threadSize, 1, 1}>([particles, count, dt, resetHeight, reset]() {
                uint32_t id = Ccino::ComputeGlobalID.x;
                if (id >= count) {
                    return;
                }
                if (reset != 0) {
                    Ccino::Vec3 p = particles[id].restPos + Ccino::Vec3(0.0f, resetHeight, 0.0f);
                    particles[id].pos = p;
                    particles[id].prevPos = p;
                    particles[id].vel = Ccino::Vec3(0.0f, 0.0f, 0.0f);
                    return;
                }
                if (particles[id].w > 0.0f) {
                    Ccino::Vec3 pos = particles[id].pos;
                    particles[id].prevPos = pos;
                    Ccino::Vec3 vel = particles[id].vel + Ccino::Vec3(0.0f, -9.8f, 0.0f) * dt;
                    particles[id].vel = vel;
                    particles[id].pos = pos + vel * dt;
                }
            }, Ccino::WorkGroupSize{(count + g_threadSize - 1) / g_threadSize, 1, 1});
        });
    }
}

void XPBDSystem::AddVolumeConstraint(Ccino::RGBuilder& rgBuilder, const Ccino::FrameContext& frame, bool resetLambda) {
    EnsureRegistered(rgBuilder);
    uint32_t reset = resetLambda ? 1u : 0u;
    for (const auto& obj : m_objects) {
        if (obj.volumeRID == RID::Invalid()) continue;
        RID particleRID = obj.particleRID;
        RID vcRID = obj.volumeRID;
        for (const auto& batch : obj.volumeConstraints.batches) {
            //这里循环不会导致Pass重复，因为Pass和Shader绑定，这里的Shader 是钉死的只是参数不同
            rgBuilder.AddPass<"VolumeConstraint">([this, &frame, particleRID, vcRID, batch, reset](Ccino::RGBuilder& rg, RID& pass) mutable {
                auto particles = rg.ReadWrite<Particle>(particleRID, pass);
                auto vc = rg.ReadWrite<VolumeConstraint>(vcRID, pass);
                uint32_t offset = batch.offset;
                uint32_t count = batch.count;
                float dt = frame.dt > 0.0f ? frame.dt : (1.0f / 60.0f);
                float a = alpha / dt / dt;
                float b = beta * dt * dt;
                float gamma = a * b / dt;

                rg.AddCS<Ccino::ThreadGroupSize{g_threadSize, 1, 1}>([particles, vc, offset, count, a, gamma, reset]() {
                    uint32_t gid = Ccino::ComputeGlobalID.x;
                    if (gid >= count) {
                        return;
                    }
                    uint32_t idx = offset + gid;
                    float lambda = vc[idx].lambda;
                    if (reset != 0u) {
                        lambda = 0.0f;
                    }

                    std::array<Ccino::Vec3, 8> p;
                    for (int i = 0; i < 8; i++) {
                        p[i] = particles[vc[idx].particles[i]].pos;
                    }
                    float c = 0.0f;
                    std::array<Ccino::Vec3, 8> grad;
                    VolumeConstraint::GetConstraint_GPU(p, vc[idx].origin, c, grad);

                    float denomSum = 0.0f;
                    float dampTerm = 0.0f;
                    for (int i = 0; i < 8; i++) {
                        uint32_t pid = vc[idx].particles[i];
                        float w = particles[pid].w;
                        Ccino::Vec3 g = grad[i];
                        denomSum = denomSum + w * Ccino::Dot(g, g);
                        dampTerm = dampTerm + Ccino::Dot(g, particles[pid].pos - particles[pid].prevPos);
                    }

                    float denom = (1.0f + gamma) * denomSum + a;
                    if (denom > 1e-12f) {
                        float dLambda = (-c - a * lambda - gamma * dampTerm) / denom;
                        vc[idx].lambda = lambda + dLambda;
                        for (int i = 0; i < 8; i++) {
                            uint32_t pid = vc[idx].particles[i];
                            float w = particles[pid].w;
                            particles[pid].pos = particles[pid].pos + grad[i] * (w * dLambda);
                        }
                    } else if (reset != 0u) {
                        vc[idx].lambda = 0.0f;
                    }
                }, Ccino::WorkGroupSize{(batch.count + g_threadSize - 1) / g_threadSize, 1, 1});
            });
        }
    }
}

void XPBDSystem::AddTerraConstraint(Ccino::RGBuilder& rgBuilder, const Ccino::FrameContext&) {
    EnsureRegistered(rgBuilder);
    for (const auto& obj : m_objects) {
        RID particleRID = obj.particleRID;
        uint32_t count = obj.particleCount;
        rgBuilder.AddPass<"TerraConstraint">([particleRID, count](Ccino::RGBuilder& rg, RID& pass) mutable {
            auto particles = rg.ReadWrite<Particle>(particleRID, pass);
            float groundY = kGroundY;

            rg.AddCS<Ccino::ThreadGroupSize{g_threadSize, 1, 1}>([particles, count, groundY]() {
                uint32_t id = Ccino::ComputeGlobalID.x;
                if (id >= count) {
                    return;
                }
                if (particles[id].pos.y < groundY) {
                    particles[id].pos.y = groundY;
                }
            }, Ccino::WorkGroupSize{(count + g_threadSize - 1) / g_threadSize, 1, 1});
        });
    }
}

void XPBDSystem::AddBounceConstraint(Ccino::RGBuilder& rgBuilder, const Ccino::FrameContext& frame, bool resetLambda) {
    EnsureRegistered(rgBuilder);
    uint32_t reset = resetLambda ? 1u : 0u;
    for (const auto& obj : m_objects) {
        if (obj.bounceRID == RID::Invalid()) continue;
        RID particleRID = obj.particleRID;
        RID bcRID = obj.bounceRID;
        for (const auto& batch : obj.bounceConstraints.batches) {
            rgBuilder.AddPass<"BounceConstraint">([this, &frame, particleRID, bcRID, batch, reset](Ccino::RGBuilder& rg, RID& pass) mutable {
                auto particles = rg.ReadWrite<Particle>(particleRID, pass);
                auto bc = rg.ReadWrite<BounceConstraint>(bcRID, pass);
                uint32_t offset = batch.offset;
                uint32_t count = batch.count;
                float dt = frame.dt > 0.0f ? frame.dt : (1.0f / 60.0f);
                float a = alpha / dt / dt;
                float b = beta * dt * dt;
                float gamma = a * b / dt;

                rg.AddCS<Ccino::ThreadGroupSize{g_threadSize, 1, 1}>([particles, bc, offset, count, a, gamma, reset]() {
                    uint32_t gid = Ccino::ComputeGlobalID.x;
                    if (gid >= count) {
                        return;
                    }
                    uint32_t idx = offset + gid;
                    uint32_t p0 = bc[idx].particles[0];
                    uint32_t p1 = bc[idx].particles[1];
                    float w0 = particles[p0].w;
                    float w1 = particles[p1].w;
                    float lambda = bc[idx].lambda;
                    if (reset != 0u) {
                        lambda = 0.0f;
                    }

                    float c = 0.0f;
                    std::array<Ccino::Vec3, 2> grad;
                    BounceConstraint::GetConstraint_GPU(particles[p0].pos, particles[p1].pos, bc[idx].origin, c, grad);

                    float denomSum = w0 * Ccino::Dot(grad[0], grad[0]) + w1 * Ccino::Dot(grad[1], grad[1]);
                    float dampTerm = Ccino::Dot(grad[0], particles[p0].pos - particles[p0].prevPos)
                                   + Ccino::Dot(grad[1], particles[p1].pos - particles[p1].prevPos);

                    float denom = (1.0f + gamma) * denomSum + a;
                    if (denom > 1e-12f) {
                        float dLambda = (-c - a * lambda - gamma * dampTerm) / denom;
                        bc[idx].lambda = lambda + dLambda;
                        particles[p0].pos = particles[p0].pos + grad[0] * (w0 * dLambda);
                        particles[p1].pos = particles[p1].pos + grad[1] * (w1 * dLambda);
                    } else if (reset != 0u) {
                        bc[idx].lambda = 0.0f;
                    }
                }, Ccino::WorkGroupSize{(batch.count + g_threadSize - 1) / g_threadSize, 1, 1});
            });
        }
    }
}

void XPBDSystem::AddUpdateVelocity(Ccino::RGBuilder& rgBuilder, const Ccino::FrameContext& frame) {
    EnsureRegistered(rgBuilder);
    for (const auto& obj : m_objects) {
        RID particleRID = obj.particleRID;
        uint32_t count = obj.particleCount;
        rgBuilder.AddPass<"UpdateVelocity">([&frame, particleRID, count](Ccino::RGBuilder& rg, RID& pass) mutable {
            auto particles = rg.ReadWrite<Particle>(particleRID, pass);
            float dt = frame.dt > 0.0f ? frame.dt : (1.0f / 60.0f);
            float groundY = kGroundY;

            rg.AddCS<Ccino::ThreadGroupSize{g_threadSize, 1, 1}>([particles, count, dt, groundY]() {
                uint32_t id = Ccino::ComputeGlobalID.x;
                if (id >= count) {
                    return;
                }
                if (particles[id].w > 0.0f) {
                    Ccino::Vec3 vel = (particles[id].pos - particles[id].prevPos) / dt;
                    if (particles[id].pos.y <= groundY + 1e-4f) {
                        vel.x = vel.x * 0.92f;
                        vel.z = vel.z * 0.92f;
                    }
                    particles[id].vel = vel;
                }
            }, Ccino::WorkGroupSize{(count + g_threadSize - 1) / g_threadSize, 1, 1});
        });
    }
}
