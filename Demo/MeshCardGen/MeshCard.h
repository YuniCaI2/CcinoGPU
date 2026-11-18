#pragma once
#include "CcinoType.h"
struct SurfelSample {
    Ccino::Vec3 Position;
    Ccino::Vec3 Normal;
    int32_t minRayZ;
    int32_t cellZ;
};


