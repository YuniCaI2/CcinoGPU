#pragma once
#include "CcinoType.h"
#include <array>
#include <cstdint>

struct SurfelSample {
    Ccino::Vec3 Position;
    Ccino::Vec3 Normal;
    int32_t minRayZ;
    int32_t cellZ;
};


struct Box {
    Ccino::Vec3 Min;
    Ccino::Vec3 Max;

    Ccino::Vec3 GetCenter() {
        return (Min + Max) / 2.0f;
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


class MeshCard {

};



