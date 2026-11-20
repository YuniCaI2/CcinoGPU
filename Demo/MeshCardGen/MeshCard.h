#pragma once
#include "AccelerationStructure.h"
#include "CcinoContext.h"
#include "CcinoType.h"
#include "RefCounted.hpp"
#include "RenderGraph.h"
#include <array>
#include <cstdint>


namespace MeshCardGen {
    struct SurfelSample {
        Ccino::Vec3 Position;
        Ccino::Vec3 Normal;
        int32_t minRayZ;
        int32_t cellZ;
    };


    struct Box {
        Ccino::Vec3 min;
        Ccino::Vec3 max;

        Ccino::Vec3 GetCenter() const  {
            return (min + max) / 2.0f;
        }

        Ccino::Vec3 GetExtent() const {
            return (max - min) * 0.5f;
        }
    };

    struct Surfel {
        Ccino::IVec3 coord;

        // Card's min near plane distance from this surfel
        int32_t minRayZ;
        // Percentage of rays which hit something in this cell
        float coverage;

        // Coverage weighted by the visibility of this surfel from outside the mesh, decides how important is to cover this surfel
        float weightedCoverage;

    };

    enum class CardDirection : uint8_t {
        PosX = 0, NegX,
        PosY,     NegY,
        PosZ,     NegZ,
        Count
    };


    struct SurfelScenePerDirection {
        std::vector<Surfel> surfels;
        void Init() {
            surfels.clear();
        }
    };

    struct AxisAlignedDirection {
        Ccino::Mat4 localToWorldRotation;
        Ccino::Vec3 localToWorldOffset;
        Ccino::IVec3 volumeSize;
        float voxelSize;

        Ccino::Vec3 TransformSurfel(const Ccino::IVec3& surfelCoord) {
            return Ccino::Vec3(localToWorldRotation * Ccino::Vec4(surfelCoord.x + 0.5f, surfelCoord.y + 0.5f, surfelCoord.z, 1.0f)) * voxelSize + localToWorldOffset;
        }
    };

    struct ClusteringParams {
        float voxelSize = 0.0f;
        float minClusterCoverage = 0.0f;
        float minOuterClusterCoverage = 0.0f;
        float minDensityPerCluster = 0.0f;
        int maxLumenMeshCards = 0;

        std::array<AxisAlignedDirection, static_cast<size_t>(CardDirection::Count)> clusterBasis;
    };



    struct SurfelScene {
        std::array<SurfelScenePerDirection, static_cast<size_t>(CardDirection::Count)> directions;
        int32_t numSurfels = 0;

        void Init() {
            for (auto& dir : directions) {
                dir.Init();
            }
            numSurfels = 0;
        }
        void AddSurfel(CardDirection dir, const Surfel& s) {
            directions[static_cast<size_t>(dir)].surfels.push_back(s);
            ++numSurfels;
        }
    };

    void InitClusteringParams(ClusteringParams& clusteringParams, const Box& MeshCardsBounds, uint32_t maxVoxels, uint32_t maxLumenMeshCards);


    void GenerateSurfaceForDirection(Ccino::RGBuilder& rgBuilder, const Ccino::Ref<Ccino::TLAS>& meshASContext, const AxisAlignedDirection& aaDirection,
        const std::vector<Ccino::Vec3>& rayDirectionOverHemisphere, const ClusteringParams& clusteringParam, const SurfelScenePerDirection& SurfelScenePerDirection);

}
