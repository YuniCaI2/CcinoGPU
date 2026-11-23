#include "MeshCard.h"
#include "RHI/Core.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace MeshCardGen {

    namespace {
        constexpr float kPi = 3.14159265358979323846f;

        Ccino::Vec3 DirectionToNormal(uint32_t dirIndex) {
            Ccino::Vec3 n(0.0f);
            n[dirIndex / 2] = (dirIndex & 1u) ? 1.0f : -1.0f;
            return n;
        }

        std::array<HemisphereRayGPU, kNumHemiRays> GenerateHemisphereSamples() {
            // 对应 UE MeshUtilities::GenerateStratifiedUniformHemisphereSamples (8x4 分层均匀半球采样)
            std::array<HemisphereRayGPU, kNumHemiRays> samples{};
            constexpr uint32_t kNumTheta = 4;
            constexpr uint32_t kNumPhi = 8;
            for (uint32_t t = 0; t < kNumTheta; ++t) {
                for (uint32_t p = 0; p < kNumPhi; ++p) {
                    const uint32_t idx = t * kNumPhi + p;
                    const float u = (static_cast<float>(t) + 0.5f) / static_cast<float>(kNumTheta);
                    const float v = (static_cast<float>(p) + 0.5f) / static_cast<float>(kNumPhi);
                    const float cosTheta = u;
                    const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
                    const float phi = 2.0f * kPi * v;
                    samples[idx].dir = Ccino::Vec3(std::cos(phi) * sinTheta, std::sin(phi) * sinTheta, cosTheta);
                    samples[idx].pad = 0.0f;
                }
            }
            return samples;
        }
    } // namespace

    void BuildSphereMesh(float radius, uint32_t stacks, uint32_t slices,
                         std::vector<SphereVertex>& outVertices,
                         std::vector<uint32_t>& outIndices,
                         Box& outBounds) {
        outVertices.clear();
        outIndices.clear();

        for (uint32_t y = 0; y <= stacks; ++y) {
            const float v = static_cast<float>(y) / static_cast<float>(stacks);
            const float phi = v * kPi;
            const float sinPhi = std::sin(phi);
            const float cosPhi = std::cos(phi);

            for (uint32_t x = 0; x <= slices; ++x) {
                const float u = static_cast<float>(x) / static_cast<float>(slices);
                const float theta = u * 2.0f * kPi;
                const Ccino::Vec3 n(std::cos(theta) * sinPhi, cosPhi, std::sin(theta) * sinPhi);
                outVertices.push_back(SphereVertex{
                    .pos = n * radius,
                    .pad0 = 0.0f,
                    .normal = n,
                    .pad1 = 0.0f,
                });
            }
        }

        const uint32_t stride = slices + 1;
        auto pushTri = [&](uint32_t a, uint32_t b, uint32_t c) {
            const Ccino::Vec3& p0 = outVertices[a].pos;
            const Ccino::Vec3& p1 = outVertices[b].pos;
            const Ccino::Vec3& p2 = outVertices[c].pos;
            const Ccino::Vec3 fn = glm::cross(p1 - p0, p2 - p0);
            if (glm::dot(fn, fn) < 1e-12f) {
                return;
            }
            // 确保所有三角形绕序与外法线一致, 方便 RayQuery 几何法线与背面剔除
            if (glm::dot(fn, p0 + p1 + p2) < 0.0f) {
                std::swap(b, c);
            }
            outIndices.push_back(a);
            outIndices.push_back(b);
            outIndices.push_back(c);
        };

        for (uint32_t y = 0; y < stacks; ++y) {
            for (uint32_t x = 0; x < slices; ++x) {
                const uint32_t i0 = y * stride + x;
                const uint32_t i1 = i0 + 1;
                const uint32_t i2 = (y + 1) * stride + x;
                const uint32_t i3 = i2 + 1;
                if (y > 0) {
                    pushTri(i0, i2, i1);
                }
                if (y + 1 < stacks) {
                    pushTri(i1, i2, i3);
                }
            }
        }

        outBounds.min = Ccino::Vec3(-radius);
        outBounds.max = Ccino::Vec3(radius);
    }

    void InitAxisBasis(const Box& meshBounds, int maxVoxels, float minClusterCoverage,
                       std::array<AxisBasisGPU, kNumDirections>& outBasis,
                       float& outMinOuterCoverage) {
        const int clampedMaxVoxels = std::clamp(maxVoxels, 4, static_cast<int>(kMaxVoxels));

        // 对应 UE BuildMeshCards: 稍微外扩包围盒避免表面正好落在边界上
        const Ccino::Vec3 center = meshBounds.GetCenter();
        const Ccino::Vec3 extent = meshBounds.GetExtent() + Ccino::Vec3(0.02f);
        const Box expandedBounds{center - extent, center + extent};

        const Ccino::Vec3 boundsSize = 2.0f * expandedBounds.GetExtent();
        const float maxBoundsDim = std::max({boundsSize.x, boundsSize.y, boundsSize.z});
        const float voxelSize = maxBoundsDim / static_cast<float>(clampedMaxVoxels);

        Ccino::IVec3 sizeInVoxels(1);
        for (int i = 0; i < 3; ++i) {
            sizeInVoxels[i] = std::clamp(static_cast<int>(std::round(boundsSize[i] / voxelSize)), 1, clampedMaxVoxels);
        }

        const Ccino::Vec3 voxelBoundsExtent = Ccino::Vec3(sizeInVoxels) * voxelSize * 0.5f;
        const Ccino::Vec3 voxelBoundsMin = center - voxelBoundsExtent;
        const Ccino::Vec3 voxelBoundsMax = center + voxelBoundsExtent;

        for (uint32_t dir = 0; dir < kNumDirections; ++dir) {
            AxisBasisGPU& basis = outBasis[dir];
            basis.voxelSize = voxelSize;

            Ccino::Vec3 xAxis(1.0f, 0.0f, 0.0f);
            Ccino::Vec3 yAxis(0.0f, 1.0f, 0.0f);
            switch (dir / 2) {
            case 0:
                xAxis = Ccino::Vec3(0.0f, 1.0f, 0.0f);
                yAxis = Ccino::Vec3(0.0f, 0.0f, 1.0f);
                basis.volumeSize = Ccino::IVec3(sizeInVoxels.y, sizeInVoxels.z, sizeInVoxels.x);
                break;
            case 1:
                xAxis = Ccino::Vec3(1.0f, 0.0f, 0.0f);
                yAxis = Ccino::Vec3(0.0f, 0.0f, 1.0f);
                basis.volumeSize = Ccino::IVec3(sizeInVoxels.x, sizeInVoxels.z, sizeInVoxels.y);
                break;
            default:
                xAxis = Ccino::Vec3(1.0f, 0.0f, 0.0f);
                yAxis = Ccino::Vec3(0.0f, 1.0f, 0.0f);
                basis.volumeSize = Ccino::IVec3(sizeInVoxels.x, sizeInVoxels.y, sizeInVoxels.z);
                break;
            }

            const Ccino::Vec3 cardNormal = DirectionToNormal(dir);
            basis.axisX = xAxis;
            basis.axisY = yAxis;
            basis.rayDir = -cardNormal; // 射线沿卡片朝向射入物体内部

            basis.offset = voxelBoundsMin;
            if (dir & 1u) {
                basis.offset[dir / 2] = voxelBoundsMax[dir / 2];
            }

            // 将 MeshCardsBounds 变换到该方向局部空间, 供 SerializeLOD 裁剪使用
            Ccino::Vec3 lMin(1e30f);
            Ccino::Vec3 lMax(-1e30f);
            for (int c = 0; c < 8; ++c) {
                const Ccino::Vec3 corner(
                    (c & 1) ? expandedBounds.max.x : expandedBounds.min.x,
                    (c & 2) ? expandedBounds.max.y : expandedBounds.min.y,
                    (c & 4) ? expandedBounds.max.z : expandedBounds.min.z);
                const Ccino::Vec3 d = corner - basis.offset;
                const Ccino::Vec3 lp(glm::dot(d, basis.axisX), glm::dot(d, basis.axisY), glm::dot(d, basis.rayDir));
                lMin = glm::min(lMin, lp);
                lMax = glm::max(lMax, lp);
            }
            basis.localMin = lMin;
            basis.localMax = lMax;
        }

        const float avgFaceArea = 2.0f * static_cast<float>(
            sizeInVoxels.x * sizeInVoxels.y +
            sizeInVoxels.x * sizeInVoxels.z +
            sizeInVoxels.y * sizeInVoxels.z) / 6.0f;
        outMinOuterCoverage = std::min(minClusterCoverage, 0.5f * avgFaceArea);
    }

    bool MeshCardGenerator::Init(Ccino::Context& ctx,
                                 const Ccino::Ref<Ccino::Buffer>& vertexBuffer,
                                 const Ccino::Ref<Ccino::Buffer>& indexBuffer,
                                 const Ccino::Ref<Ccino::TLAS>& tlas,
                                 const Box& meshBounds) {
        m_meshBounds = meshBounds;

        auto makeHostRW = [&](uint32_t size) {
            return ctx.CreateBuffer({
                .size = size,
                .usage = Ccino::BufferUsage::Storage,
                .hostAccess = Ccino::RHI::HostAccess::ReadWrite,
            });
        };

        m_basisBuf    = makeHostRW(kNumDirections * sizeof(AxisBasisGPU));
        m_hemiBuf     = makeHostRW(kNumHemiRays * sizeof(HemisphereRayGPU));
        m_paramsBuf   = makeHostRW(sizeof(GenParamsGPU));
        m_surfelsBuf  = makeHostRW(kMaxTotalSurfels * sizeof(SurfelGPU));
        m_cardsBuf    = makeHostRW(kMaxTotalCards * sizeof(MeshCardGPU));
        m_countersBuf = makeHostRW(12 * sizeof(CounterElem));

        const auto hemiSamples = GenerateHemisphereSamples();
        m_hemiBuf->Copy(hemiSamples.data());

        m_genBuilder = ctx.CreateRGBuilder("MeshCardGenCompute");
        RID vbRID       = m_genBuilder->CreateExternalBuffer(vertexBuffer);
        RID ibRID       = m_genBuilder->CreateExternalBuffer(indexBuffer);
        RID tlasRID     = m_genBuilder->CreateExternalTLAS(tlas);
        RID basisRID    = m_genBuilder->CreateExternalBuffer(m_basisBuf);
        RID hemiRID     = m_genBuilder->CreateExternalBuffer(m_hemiBuf);
        RID paramsRID   = m_genBuilder->CreateExternalBuffer(m_paramsBuf);
        RID surfelsRID  = m_genBuilder->CreateExternalBuffer(m_surfelsBuf);
        RID cardsRID    = m_genBuilder->CreateExternalBuffer(m_cardsBuf);
        RID countersRID = m_genBuilder->CreateExternalBuffer(m_countersBuf);

        // =========================================================================
        // Pass 1: 体素化 + RayQuery 求交 + 半球可见性 -> 生成 Surfel
        // 对应 UE GenerateSurfelsForDirection + ComputeSurfelVisibility
        // 每个线程处理 (coordX, coordY, dir) 一整列体素
        // =========================================================================
        m_genBuilder->AddPass<"MeshCardGenSurfelsPass">([=](Ccino::RGBuilder& rg, RID& pass) mutable {
            auto sceneAS  = rg.ReadAS(tlasRID, pass);
            auto vb       = rg.Read<SphereVertex>(vbRID, pass);
            auto ib       = rg.Read<IndexElem>(ibRID, pass);
            auto bases    = rg.Read<AxisBasisGPU>(basisRID, pass);
            auto hemiRays = rg.Read<HemisphereRayGPU>(hemiRID, pass);
            auto params   = rg.Read<GenParamsGPU>(paramsRID, pass);
            auto surfels  = rg.ReadWrite<SurfelGPU>(surfelsRID, pass);
            auto counters = rg.ReadWrite<CounterElem>(countersRID, pass);

            uint32_t maxTotalSurfels = kMaxTotalSurfels;
            uint32_t invalidCluster  = kInvalidClusterId;

            rg.AddCS<Ccino::ThreadGroupSize{8, 8, 1}>([=]() {
                uint32_t coordX = Ccino::ComputeGlobalID.x;
                uint32_t coordY = Ccino::ComputeGlobalID.y;
                uint32_t dir    = Ccino::ComputeGlobalID.z;
                if (dir >= 6u) {
                    return;
                }

                AxisBasisGPU basis = bases[dir];
                if (int32_t(coordX) >= basis.volumeSize.x || int32_t(coordY) >= basis.volumeSize.y) {
                    return;
                }

                uint32_t numSamples      = params[0].numSamples;
                uint32_t numVisRays      = params[0].numVisRays;
                float    normalThreshold = params[0].normalThreshold;
                float    voxelSize       = basis.voxelSize;
                float    nearPlaneOffset = 2.0f * voxelSize;

                // 记录当前 (coordX, coordY) 列中命中的若干 (cellZ, minRayZ) 层 (对应 UE 的 span 聚合)
                std::array<int32_t, 4>     layerZ;
                std::array<int32_t, 4>     layerMinRayZ;
                std::array<uint32_t, 4>    layerCount;
                std::array<Ccino::Vec3, 4> layerPos;
                std::array<Ccino::Vec3, 4> layerNormal;
                uint32_t numLayers = 0u;

                for (uint32_t s = 0u; s < numSamples; ++s) {
                    float jx = (float(s) + 0.5f) / float(numSamples);
                    float jy = float(Ccino::BitfieldReverse(s)) * 2.3283064365386963e-10f;

                    Ccino::Vec3 rayOrigin = basis.offset
                        + (basis.axisX * (float(coordX) + jx) + basis.axisY * (float(coordY) + jy)) * voxelSize
                        - basis.rayDir * nearPlaneOffset;

                    int32_t lastHitZ = -2;
                    float   rayTNear = 0.0f;

                    for (uint32_t step = 0u; step < 8u; ++step) {
                        if (lastHitZ + 1 >= basis.volumeSize.z) {
                            break;
                        }

                        Ccino::RayQueryEXT rq;
                        Ccino::RayQueryInitializeEXT(
                            rq, sceneAS, Ccino::RayFlagsOpaqueEXT, 0xFFu,
                            rayOrigin, rayTNear, basis.rayDir, 1000.0f);
                        while (Ccino::RayQueryProceedEXT(rq)) {
                        }

                        if (Ccino::RayQueryGetIntersectionTypeEXT(rq, true) != Ccino::RayQueryCommittedIntersectionTriangleEXT) {
                            break;
                        }

                        float hitT = Ccino::RayQueryGetIntersectionTEXT(rq, true);
                        int32_t primId = Ccino::RayQueryGetIntersectionPrimitiveIndexEXT(rq, true);

                        uint32_t i0 = ib[primId * 3 + 0].value;
                        uint32_t i1 = ib[primId * 3 + 1].value;
                        uint32_t i2 = ib[primId * 3 + 2].value;
                        Ccino::Vec3 p0 = vb[i0].pos;
                        Ccino::Vec3 p1 = vb[i1].pos;
                        Ccino::Vec3 p2 = vb[i2].pos;
                        Ccino::Vec3 hitNormal = Ccino::Normalize(Ccino::Cross(p1 - p0, p2 - p0));

                        int32_t hitZ = Ccino::Clamp(
                            int32_t((hitT - nearPlaneOffset) / voxelSize),
                            0, basis.volumeSize.z - 1);

                        float nDotD = Ccino::Dot(-basis.rayDir, hitNormal);
                        if (nDotD >= normalThreshold && hitZ > lastHitZ + 1) {
                            int32_t minRayZ = (lastHitZ >= 0) ? (lastHitZ + 1) : 0;
                            Ccino::Vec3 hitPos = rayOrigin + basis.rayDir * hitT;

                            bool merged = false;
                            for (uint32_t k = 0u; k < numLayers; ++k) {
                                if (layerZ[k] == hitZ && layerMinRayZ[k] == minRayZ) {
                                    layerCount[k] = layerCount[k] + 1u;
                                    merged = true;
                                    break;
                                }
                            }
                            if (!merged && numLayers < 4u) {
                                layerZ[numLayers]       = hitZ;
                                layerMinRayZ[numLayers] = minRayZ;
                                layerCount[numLayers]   = 1u;
                                layerPos[numLayers]     = hitPos;
                                layerNormal[numLayers]  = hitNormal;
                                numLayers = numLayers + 1u;
                            }
                        }

                        lastHitZ = hitZ;
                        float safeT = Ccino::Max(hitT, rayTNear);
                        rayTNear = Ccino::Max(nearPlaneOffset + float(lastHitZ + 1) * voxelSize, safeT + 0.0005f);
                    }
                }

                // 对每个候选层执行半球可见性测试 (对应 UE ComputeSurfelVisibility), 过滤物体内部 Surfel
                for (uint32_t k = 0u; k < numLayers; ++k) {
                    Ccino::Vec3 n = layerNormal[k];
                    Ccino::Vec3 t1;
                    Ccino::Vec3 t2;
                    if (n.z < -0.9999f) {
                        t1 = Ccino::Vec3(0.0f, -1.0f, 0.0f);
                        t2 = Ccino::Vec3(-1.0f, 0.0f, 0.0f);
                    } else {
                        float a = 1.0f / (1.0f + n.z);
                        float b = -n.x * n.y * a;
                        t1 = Ccino::Vec3(1.0f - n.x * n.x * a, b, -n.x);
                        t2 = Ccino::Vec3(b, 1.0f - n.y * n.y * a, -n.y);
                    }

                    uint32_t numHits = 0u;
                    uint32_t numBackFaceHits = 0u;
                    float visibilitySum = 0.0f;
                    float rayBias = voxelSize * 0.05f;

                    for (uint32_t r = 0u; r < numVisRays; ++r) {
                        Ccino::Vec3 h = hemiRays[r].dir;
                        Ccino::Vec3 hDir = Ccino::Normalize(t1 * h.x + t2 * h.y + n * h.z);

                        Ccino::RayQueryEXT vrq;
                        Ccino::RayQueryInitializeEXT(
                            vrq, sceneAS, Ccino::RayFlagsOpaqueEXT, 0xFFu,
                            layerPos[k], rayBias, hDir, 1000.0f);
                        while (Ccino::RayQueryProceedEXT(vrq)) {
                        }

                        if (Ccino::RayQueryGetIntersectionTypeEXT(vrq, true) == Ccino::RayQueryCommittedIntersectionTriangleEXT) {
                            numHits = numHits + 1u;
                            int32_t pid = Ccino::RayQueryGetIntersectionPrimitiveIndexEXT(vrq, true);
                            uint32_t vi0 = ib[pid * 3 + 0].value;
                            uint32_t vi1 = ib[pid * 3 + 1].value;
                            uint32_t vi2 = ib[pid * 3 + 2].value;
                            Ccino::Vec3 hn = Ccino::Normalize(Ccino::Cross(vb[vi1].pos - vb[vi0].pos, vb[vi2].pos - vb[vi0].pos));
                            if (Ccino::Dot(hDir, hn) > 0.0f) {
                                numBackFaceHits = numBackFaceHits + 1u;
                            }
                        } else {
                            visibilitySum = visibilitySum + 1.0f;
                        }
                    }

                    bool insideGeom = (float(numHits) > 0.8f * float(numVisRays))
                                   && (float(numBackFaceHits) > 0.2f * float(numVisRays));
                    if (!insideGeom) {
                        uint32_t slot = Ccino::AtomicAdd(counters[6].value, uint32_t(1));
                        if (slot < maxTotalSurfels) {
                            Ccino::AtomicAdd(counters[dir].value, uint32_t(1));
                            float visibility = visibilitySum / float(numVisRays);
                            float coverage = float(layerCount[k]) / float(numSamples);

                            SurfelGPU outSurfel;
                            outSurfel.worldPos = basis.offset
                                + (basis.axisX * (float(coordX) + 0.5f)
                                 + basis.axisY * (float(coordY) + 0.5f)
                                 + basis.rayDir * float(layerZ[k])) * voxelSize;
                            outSurfel.coverage = coverage;
                            outSurfel.worldNormal = -basis.rayDir;
                            outSurfel.weightedCoverage = coverage * (visibility + 1.0f);
                            outSurfel.coord = Ccino::IVec3(int32_t(coordX), int32_t(coordY), layerZ[k]);
                            outSurfel.minRayZ = layerMinRayZ[k];
                            outSurfel.dirIndex = dir;
                            outSurfel.valid = 1u;
                            outSurfel.clusterId = invalidCluster;
                            outSurfel.visibility = visibility;
                            surfels[slot] = outSurfel;
                        }
                    }
                }
            }, Ccino::WorkGroupSize{(kMaxVoxels + 7) / 8, (kMaxVoxels + 7) / 8, kNumDirections});
        });

        // =========================================================================
        // Pass 2: 按 6 个方向并行聚类生成候选 MeshCard (OBB)
        // 对应 UE BuildSurfelClusters + SerializeLOD
        // =========================================================================
        m_genBuilder->AddPass<"MeshCardClusterPass">([=](Ccino::RGBuilder& rg, RID& pass) mutable {
            auto bases    = rg.Read<AxisBasisGPU>(basisRID, pass);
            auto params   = rg.Read<GenParamsGPU>(paramsRID, pass);
            auto surfels  = rg.ReadWrite<SurfelGPU>(surfelsRID, pass);
            auto cards    = rg.ReadWrite<MeshCardGPU>(cardsRID, pass);
            auto counters = rg.ReadWrite<CounterElem>(countersRID, pass);

            uint32_t maxTotalSurfels = kMaxTotalSurfels;
            uint32_t maxCardsPerDir  = kMaxCardsPerDir;
            uint32_t maxTotalCards   = kMaxTotalCards;
            uint32_t invalidCluster  = kInvalidClusterId;

            rg.AddCS<Ccino::ThreadGroupSize{6, 1, 1}>([=]() {
                uint32_t dir = Ccino::ComputeGlobalID.x;
                if (dir >= 6u) {
                    return;
                }

                uint32_t totalSurfels = Ccino::Min(counters[6].value, maxTotalSurfels);
                AxisBasisGPU basis    = bases[dir];

                float minDensity         = params[0].minDensity;
                float minClusterCov      = params[0].minClusterCoverage;
                float minOuterClusterCov = params[0].minOuterClusterCoverage;

                uint32_t committedInDir = 0u;

                // 第 0 轮固定尝试 NearPlane = 0 (外层卡片), 之后迭代搜索最佳内层 NearPlane (1 .. volumeSize.z - 1)
                for (uint32_t iter = 0u; iter < maxCardsPerDir; ++iter) {
                    int32_t bestNearPlane = -1;
                    float   bestWCov      = 0.0f;
                    float   bestCov       = 0.0f;
                    float   bestDensity   = 0.0f;
                    uint32_t bestCount    = 0u;
                    Ccino::IVec3 bestMin(999999, 999999, 999999);
                    Ccino::IVec3 bestMax(-999999, -999999, -999999);

                    int32_t startZ = (iter == 0u) ? 0 : 1;
                    int32_t endZ   = (iter == 0u) ? 1 : basis.volumeSize.z;

                    for (int32_t nearPlane = startZ; nearPlane < endZ; ++nearPlane) {
                        Ccino::IVec3 bMin(999999, 999999, 999999);
                        Ccino::IVec3 bMax(-999999, -999999, -999999);
                        float cov = 0.0f;
                        float wCov = 0.0f;
                        uint32_t count = 0u;

                        for (uint32_t i = 0u; i < totalSurfels; ++i) {
                            if (surfels[i].dirIndex != dir || surfels[i].clusterId != invalidCluster) {
                                continue;
                            }
                            Ccino::IVec3 c = surfels[i].coord;
                            if (c.z >= nearPlane && surfels[i].minRayZ <= nearPlane) {
                                bMin = Ccino::Min(bMin, c);
                                bMax = Ccino::Max(bMax, c);
                                cov = cov + surfels[i].coverage;
                                wCov = wCov + surfels[i].weightedCoverage;
                                count = count + 1u;
                            }
                        }

                        if (count > 0u) {
                            float faceArea = float((bMax.x + 1 - bMin.x) * (bMax.y + 1 - bMin.y));
                            float density = cov / Ccino::Max(faceArea, 1.0f);
                            float minCovReq = (nearPlane == 0) ? minOuterClusterCov : minClusterCov;

                            if (wCov >= minCovReq && density > minDensity && wCov > bestWCov) {
                                bestNearPlane = nearPlane;
                                bestWCov      = wCov;
                                bestCov       = cov;
                                bestDensity   = density;
                                bestCount     = count;
                                bestMin       = bMin;
                                bestMax       = bMax;
                            }
                        }
                    }

                    if (bestNearPlane < 0) {
                        if (iter == 0u) {
                            continue; // 外层没有有效卡片时仍可检查内层
                        }
                        break;
                    }

                    uint32_t cardSlot = Ccino::AtomicAdd(counters[7].value, uint32_t(1));
                    if (cardSlot >= maxTotalCards) {
                        break;
                    }

                    // 标记归属该 Cluster 的所有 Surfel
                    for (uint32_t i = 0u; i < totalSurfels; ++i) {
                        if (surfels[i].dirIndex != dir || surfels[i].clusterId != invalidCluster) {
                            continue;
                        }
                        if (surfels[i].coord.z >= bestNearPlane && surfels[i].minRayZ <= bestNearPlane) {
                            surfels[i].clusterId = cardSlot;
                        }
                    }

                    // 计算卡片 OBB (对应 UE SerializeLOD)
                    float vs = basis.voxelSize;
                    Ccino::Vec3 cMin = (Ccino::Vec3(float(bestMin.x), float(bestMin.y), float(bestMin.z)) - Ccino::Vec3(0.0f, 0.0f, 0.5f)) * vs;
                    Ccino::Vec3 cMax = (Ccino::Vec3(float(bestMax.x), float(bestMax.y), float(bestMax.z)) + Ccino::Vec3(1.0f, 1.0f, 1.5f)) * vs;

                    float marginZ = 0.25f * vs;
                    cMin.x = Ccino::Max(cMin.x, basis.localMin.x);
                    cMin.y = Ccino::Max(cMin.y, basis.localMin.y);
                    cMin.z = Ccino::Max(cMin.z, basis.localMin.z - marginZ);
                    cMax.x = Ccino::Min(cMax.x, basis.localMax.x);
                    cMax.y = Ccino::Min(cMax.y, basis.localMax.y);
                    cMax.z = Ccino::Min(cMax.z, basis.localMax.z + marginZ);

                    Ccino::Vec3 localOrigin = (cMin + cMax) * 0.5f;
                    Ccino::Vec3 extent      = Ccino::Max((cMax - cMin) * 0.5f, Ccino::Vec3(0.01f));
                    Ccino::Vec3 worldOrigin = basis.offset
                        + basis.axisX * localOrigin.x
                        + basis.axisY * localOrigin.y
                        + basis.rayDir * localOrigin.z;

                    MeshCardGPU card;
                    card.origin           = worldOrigin;
                    card.weightedCoverage = bestWCov;
                    card.extent           = extent;
                    card.coverage         = bestCov;
                    card.axisX            = basis.axisX;
                    card.dirIndex         = dir;
                    card.axisY            = basis.axisY;
                    card.nearPlane        = bestNearPlane;
                    card.axisZ            = -basis.rayDir;
                    card.surfelCount      = bestCount;
                    card.boundsMin        = bestMin;
                    card.isActive         = 1u;
                    card.boundsMax        = bestMax;
                    card.density          = bestDensity;
                    card.cardId           = cardSlot;
                    card.pad0             = 0u;
                    card.pad1             = 0u;
                    card.pad2             = 0u;
                    cards[cardSlot]       = card;

                    committedInDir = committedInDir + 1u;
                }
            }, Ccino::WorkGroupSize{1, 1, 1});
        });

        // =========================================================================
        // Pass 3: 按 weightedCoverage 降序排序并截取 Top-K MeshCard
        // 对应 UE LimitClusters
        // =========================================================================
        m_genBuilder->AddPass<"MeshCardTopKPass">([=](Ccino::RGBuilder& rg, RID& pass) mutable {
            auto params   = rg.Read<GenParamsGPU>(paramsRID, pass);
            auto surfels  = rg.ReadWrite<SurfelGPU>(surfelsRID, pass);
            auto cards    = rg.ReadWrite<MeshCardGPU>(cardsRID, pass);
            auto counters = rg.ReadWrite<CounterElem>(countersRID, pass);

            uint32_t maxTotalSurfels = kMaxTotalSurfels;
            uint32_t maxTotalCards   = kMaxTotalCards;
            uint32_t invalidCluster  = kInvalidClusterId;

            rg.AddCS<Ccino::ThreadGroupSize{1, 1, 1}>([=]() {
                uint32_t totalCards   = Ccino::Min(counters[7].value, maxTotalCards);
                uint32_t totalSurfels = Ccino::Min(counters[6].value, maxTotalSurfels);
                uint32_t maxCards     = params[0].maxMeshCards;

                for (uint32_t i = 0u; i < totalCards; ++i) {
                    uint32_t best = i;
                    for (uint32_t j = i + 1u; j < totalCards; ++j) {
                        if (cards[j].weightedCoverage > cards[best].weightedCoverage) {
                            best = j;
                        }
                    }
                    if (best != i) {
                        MeshCardGPU tmp = cards[i];
                        cards[i] = cards[best];
                        cards[best] = tmp;
                    }
                }

                uint32_t topK = Ccino::Min(totalCards, maxCards);
                counters[8].value = topK;
                for (uint32_t i = 0u; i < totalCards; ++i) {
                    cards[i].isActive = (i < topK) ? 1u : 0u;
                }

                // 将每个 Surfel 的 clusterId 重映射到排序后的 Top-K 下标 (落选则标记无效)
                for (uint32_t s = 0u; s < totalSurfels; ++s) {
                    uint32_t oldId = surfels[s].clusterId;
                    uint32_t newRank = invalidCluster;
                    if (oldId != invalidCluster) {
                        for (uint32_t c = 0u; c < topK; ++c) {
                            if (cards[c].cardId == oldId) {
                                newRank = c;
                                break;
                            }
                        }
                    }
                    surfels[s].clusterId = newRank;
                }
            }, Ccino::WorkGroupSize{1, 1, 1});
        });

        return true;
    }

    void MeshCardGenerator::Destroy(Ccino::Context& ctx) {
        m_basisBuf.Reset();
        m_hemiBuf.Reset();
        m_paramsBuf.Reset();
        m_surfelsBuf.Reset();
        m_cardsBuf.Reset();
        m_countersBuf.Reset();
        if (m_genBuilder != nullptr) {
            ctx.DestroyRGBuilder("MeshCardGenCompute");
            m_genBuilder = nullptr;
        }
    }

    void MeshCardGenerator::Generate(Ccino::Context& ctx, const GenSettings& settings, uint32_t repeatCount) {
        if (m_genBuilder == nullptr) {
            return;
        }
        repeatCount = std::max(repeatCount, 1u);

        float minOuterCoverage = settings.minClusterCoverage;
        InitAxisBasis(m_meshBounds, settings.maxVoxels, settings.minClusterCoverage, m_basis, minOuterCoverage);

        GenParamsGPU gpuParams{};
        gpuParams.numSamples              = static_cast<uint32_t>(std::clamp(settings.raysPerCell, 1, 32));
        gpuParams.numVisRays              = static_cast<uint32_t>(std::clamp(settings.visRays, 1, static_cast<int>(kNumHemiRays)));
        gpuParams.maxMeshCards            = static_cast<uint32_t>(std::clamp(settings.maxMeshCards, 0, static_cast<int>(kMaxTotalCards)));
        gpuParams.normalThreshold         = settings.normalThreshold;
        gpuParams.minDensity              = settings.minDensity;
        gpuParams.minClusterCoverage      = settings.minClusterCoverage;
        gpuParams.minOuterClusterCoverage = minOuterCoverage;

        VkQueue graphicsQueue = ctx.GetRHIContext().GetCore().GetVkQueue(CommandQueueType::GRAPHICS);
        vkQueueWaitIdle(graphicsQueue);

        m_basisBuf->Copy(m_basis.data());
        m_paramsBuf->Copy(&gpuParams);

        std::array<CounterElem, 12> zeroCounters{};
        double totalMs = 0.0;
        double lastMs = 0.0;

        for (uint32_t r = 0; r < repeatCount; ++r) {
            m_countersBuf->Copy(zeroCounters.data());

            const auto t0 = std::chrono::steady_clock::now();
            m_genBuilder->Run();
            vkQueueWaitIdle(graphicsQueue);
            const auto t1 = std::chrono::steady_clock::now();

            lastMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
            totalMs += lastMs;
        }

        // 回读统计与 Top-K 卡片列表供 ImGui 展示
        std::array<CounterElem, 12> counters{};
        m_countersBuf->Readback(counters.data());

        std::array<MeshCardGPU, kMaxTotalCards> allCards{};
        m_cardsBuf->Readback(allCards.data());

        m_stats.lastGpuMs = lastMs;
        if (repeatCount > 1) {
            m_stats.avgGpuMs = totalMs / static_cast<double>(repeatCount);
            m_stats.runCount = repeatCount;
        } else {
            m_stats.runCount += 1;
            m_stats.avgGpuMs = (m_stats.runCount == 1)
                ? lastMs
                : (m_stats.avgGpuMs * 0.8 + lastMs * 0.2);
        }

        for (uint32_t d = 0; d < kNumDirections; ++d) {
            m_stats.dirSurfels[d] = counters[d].value;
        }
        m_stats.totalSurfels   = std::min(counters[6].value, kMaxTotalSurfels);
        m_stats.candidateCards = std::min(counters[7].value, kMaxTotalCards);
        m_stats.topKCards      = std::min(counters[8].value, kMaxTotalCards);

        m_stats.cards.assign(allCards.begin(), allCards.begin() + m_stats.topKCards);
    }

} // namespace MeshCardGen
