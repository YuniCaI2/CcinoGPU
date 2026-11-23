#pragma once
#include "Context/CcinoContext.h"
#include "Core/AccelerationStructure.h"
#include "Core/Buffer.h"
#include "Core/CcinoType.h"
#include "RG/RenderGraph.h"

#include <array>
#include <cstdint>
#include <vector>

namespace MeshCardGen {

    inline constexpr uint32_t kNumDirections      = 6;
    inline constexpr uint32_t kMaxVoxels          = 32;
    inline constexpr uint32_t kMaxSurfelsPerDir   = 4096;
    inline constexpr uint32_t kMaxTotalSurfels    = kNumDirections * kMaxSurfelsPerDir;
    inline constexpr uint32_t kMaxCardsPerDir     = 8;
    inline constexpr uint32_t kMaxTotalCards      = kNumDirections * kMaxCardsPerDir;
    inline constexpr uint32_t kNumHemiRays        = 32;
    inline constexpr uint32_t kInvalidClusterId   = 0xFFFFFFFFu;

    struct Box {
        Ccino::Vec3 min{0.0f};
        Ccino::Vec3 max{0.0f};

        Ccino::Vec3 GetCenter() const { return (min + max) * 0.5f; }
        Ccino::Vec3 GetExtent() const { return (max - min) * 0.5f; }
    };

    // 所有 GPU SSBO 元素均按 4 字节整数倍打包、不含 bool, 保证 C++ 与 GLSL scalar 布局一致
    struct SphereVertex {
        Ccino::Vec3 pos;
        float pad0;
        Ccino::Vec3 normal;
        float pad1;
    };

    struct IndexElem {
        uint32_t value;
    };

    struct CounterElem {
        uint32_t value;
    };

    struct AxisBasisGPU {
        Ccino::Vec3 axisX;      float voxelSize;
        Ccino::Vec3 axisY;      float pad0;
        Ccino::Vec3 rayDir;     float pad1;  // 射线步进方向 (= -cardNormal)
        Ccino::Vec3 offset;     float pad2;  // localToWorldOffset
        Ccino::IVec3 volumeSize; int32_t pad3;
        Ccino::Vec3 localMin;   float pad4;  // MeshBounds 投影到该方向局部空间的 min
        Ccino::Vec3 localMax;   float pad5;  // MeshBounds 投影到该方向局部空间的 max
    };

    struct HemisphereRayGPU {
        Ccino::Vec3 dir;
        float pad;
    };

    struct GenParamsGPU {
        uint32_t numSamples;
        uint32_t numVisRays;
        uint32_t maxMeshCards;
        uint32_t pad0;
        float normalThreshold;
        float minDensity;
        float minClusterCoverage;
        float minOuterClusterCoverage;
    };

    struct SurfelGPU {
        Ccino::Vec3 worldPos;
        float coverage;
        Ccino::Vec3 worldNormal;
        float weightedCoverage;
        Ccino::IVec3 coord;
        int32_t minRayZ;
        uint32_t dirIndex;
        uint32_t valid;
        uint32_t clusterId;
        float visibility;
    };

    struct MeshCardGPU {
        Ccino::Vec3 origin;     float weightedCoverage;
        Ccino::Vec3 extent;     float coverage;
        Ccino::Vec3 axisX;      uint32_t dirIndex;
        Ccino::Vec3 axisY;      int32_t nearPlane;
        Ccino::Vec3 axisZ;      uint32_t surfelCount; // 卡片朝外法线 (= -rayDir)
        Ccino::IVec3 boundsMin; uint32_t isActive;    // 1 表示入选 Top-K
        Ccino::IVec3 boundsMax; float density;
        uint32_t cardId;        uint32_t pad0;
        uint32_t pad1;          uint32_t pad2;
    };

    // UI 可调参数 (对应 UE MeshCardRepresentationUtilities)
    struct GenSettings {
        int maxVoxels = 16;
        int maxMeshCards = 6;
        int raysPerCell = 16;
        int visRays = 16;
        float normalThreshold = 0.5f;      // cos(60 deg)
        float minDensity = 0.1f;
        float minClusterCoverage = 2.0f;
    };

    struct GenStats {
        double lastGpuMs = 0.0;
        double avgGpuMs = 0.0;
        uint32_t runCount = 0;
        uint32_t totalSurfels = 0;
        uint32_t candidateCards = 0;
        uint32_t topKCards = 0;
        std::array<uint32_t, kNumDirections> dirSurfels{};
        std::vector<MeshCardGPU> cards; // 已按 weightedCoverage 降序排列的前 topKCards 张
    };

    // 生成单位球网格 (保证所有三角形面法线朝外)
    void BuildSphereMesh(float radius, uint32_t stacks, uint32_t slices,
                         std::vector<SphereVertex>& outVertices,
                         std::vector<uint32_t>& outIndices,
                         Box& outBounds);

    // 对应 UE InitClusteringParams: 计算体素尺寸与 6 个轴对齐方向的局部坐标系
    void InitAxisBasis(const Box& meshBounds, int maxVoxels, float minClusterCoverage,
                       std::array<AxisBasisGPU, kNumDirections>& outBasis,
                       float& outMinOuterCoverage);

    class MeshCardGenerator {
    public:
        bool Init(Ccino::Context& ctx,
                  const Ccino::Ref<Ccino::Buffer>& vertexBuffer,
                  const Ccino::Ref<Ccino::Buffer>& indexBuffer,
                  const Ccino::Ref<Ccino::TLAS>& tlas,
                  const Box& meshBounds);

        void Destroy(Ccino::Context& ctx);

        // 在 GPU 上执行完整 MeshCard 生成 (体素化 RayQuery -> Surfel -> 聚类 -> Top-K 排序) 并记录耗时
        void Generate(Ccino::Context& ctx, const GenSettings& settings, uint32_t repeatCount = 1);

        const GenStats& GetStats() const { return m_stats; }
        float GetVoxelSize() const { return m_basis[0].voxelSize; }

        Ccino::Ref<Ccino::Buffer> GetSurfelsBuffer() const { return m_surfelsBuf; }
        Ccino::Ref<Ccino::Buffer> GetCardsBuffer() const { return m_cardsBuf; }

    private:
        Box m_meshBounds{};
        std::array<AxisBasisGPU, kNumDirections> m_basis{};

        Ccino::Ref<Ccino::Buffer> m_basisBuf;
        Ccino::Ref<Ccino::Buffer> m_hemiBuf;
        Ccino::Ref<Ccino::Buffer> m_paramsBuf;
        Ccino::Ref<Ccino::Buffer> m_surfelsBuf;
        Ccino::Ref<Ccino::Buffer> m_cardsBuf;
        Ccino::Ref<Ccino::Buffer> m_countersBuf;

        Ccino::RGBuilder* m_genBuilder = nullptr;
        GenStats m_stats{};
    };

} // namespace MeshCardGen
