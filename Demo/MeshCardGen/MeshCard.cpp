#include "MeshCard.h"
#include "CcinoType.h"
#include "RHI/RHIStruct.h"
#include "RenderGraph.h"
#include "glm/common.hpp"
#include <algorithm>


//使用GPU加速实现
namespace MeshCardGen {
    void InitClusteringParams(ClusteringParams& clusteringParams, const Box& meshCardsBounds, uint32_t maxVoxels, uint32_t maxLumenMeshCards) {
        const float targetVoxelSize = 10.0f;

        const Ccino::Vec3 meshCardsBoundsSize = 2.0f * meshCardsBounds.GetExtent();
        const float maxMeshCardsBounds = std::max({meshCardsBoundsSize.x, meshCardsBoundsSize.y, meshCardsBoundsSize.y});

        const float maxSizeInVoxels = glm::clamp(maxMeshCardsBounds / targetVoxelSize + 0.5f, 1.0f, (float)maxVoxels);
        const float voxelSize = std::max(targetVoxelSize, maxMeshCardsBounds / maxSizeInVoxels);
        Ccino::IVec3 sizeInVoxels;
        for (int i = 0; i < 3; i++) {
            sizeInVoxels[i] = glm::clamp((uint32_t)(meshCardsBoundsSize[i]/ voxelSize + 0.5f), 1u, maxVoxels);
        }

        const auto voxelBoundsCenter = meshCardsBounds.GetCenter();
        const auto voxelBoundsExtent = Ccino::Vec3(sizeInVoxels) * voxelSize * 0.5f;
        const auto voxelBoundsMin = meshCardsBounds.max;
        const auto voxelBoundsMax = meshCardsBounds.min;

        for (int index = 0; index < (size_t)CardDirection::Count; index++) {
            auto& basis = clusteringParams.clusterBasis[index];
            basis.voxelSize = voxelSize;

            auto xAxis = Ccino::Vec3(-1.0f, 0.0f, 0.0f);
            auto yAxis = Ccino::Vec3(0.0f, 1.0f, 0.0f);
            auto zAxis = Ccino::Vec3(0.0f, 0.0f, 1.0f);
            switch (index / 2) {
                case 0:
                    xAxis = Ccino::Vec3(0.0f, 0.0f, -1.0f);
                    yAxis = Ccino::Vec3(0.0f, 1.0f, 0.0f);
                    switch (index % 2) {
                        case 0:
                            zAxis = Ccino::Vec3(1.0f, 1.0f, 0.0f);
                            break;
                        case 1:
                            zAxis = Ccino::Vec3(-1.0f, 1.0f, 0.0f);
                            break;
                    }
                    break;
                case 1:
                    xAxis = Ccino::Vec3(0.0f, 0.0f, -1.0f);
                    yAxis = Ccino::Vec3(-1.0f, 0.0f, 0.0f);
                    switch (index % 2) {
                        case 0:
                            zAxis = Ccino::Vec3(0.0f, 1.0f, 0.0f);
                            break;
                        case 1:
                            zAxis = Ccino::Vec3(0.0f, -1.0f, 0.0f);
                            break;
                    }
                    break;
                case 2:
                    xAxis = Ccino::Vec3(-1.0f, 0.0f, 0.0f);
                    yAxis = Ccino::Vec3(0.0f, 1.0f, 0.0f);
                    switch (index % 2) {
                        case 0:
                            zAxis = Ccino::Vec3(0.0f, 0.0f, 1.0f);
                            break;
                        case 1:
                            zAxis = Ccino::Vec3(0.0f, 1.0f, -1.0f);
                            break;
                    }
                    break;
            }

        }
    }

    void GenerateSurfaceForDirection(Ccino::RGBuilder& rgBuilder, const Ccino::Ref<Ccino::TLAS>& meshASContext, const AxisAlignedDirection& aaDirection,
        const std::vector<Ccino::Vec3>& rayDirectionOverHemisphere, const ClusteringParams& clusteringParam, const SurfelScenePerDirection& SurfelScenePerDirection) {

        rgBuilder.AddPass<"MeshCardSurfelGenerationPass">([&](Ccino::RGBuilder& builder, RID pass){


            rgBuilder.AddCS<{8,8,1}>([=](){

            }, {1, 1, 1});
        });
    }
}
