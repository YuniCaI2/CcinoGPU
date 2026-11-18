#include "CcinoType.h"

namespace Ccino {
    void RayQueryTerminateEXT(RayQueryEXT& rayQuery) { (void)rayQuery; }
    void RayQueryGenerateIntersectionEXT(RayQueryEXT& rayQuery, float tHit) { (void)rayQuery; (void)tHit; }
    void RayQueryConfirmIntersectionEXT(RayQueryEXT& rayQuery) { (void)rayQuery; }
    bool RayQueryProceedEXT(RayQueryEXT& rayQuery) { (void)rayQuery; return false; }

    uint32_t RayQueryGetIntersectionTypeEXT(const RayQueryEXT& rayQuery, bool committed) { (void)rayQuery; (void)committed; return 0; }
    float RayQueryGetRayTMinEXT(const RayQueryEXT& rayQuery) { (void)rayQuery; return 0.0f; }
    uint32_t RayQueryGetRayFlagsEXT(const RayQueryEXT& rayQuery) { (void)rayQuery; return 0; }
    Vec3 RayQueryGetWorldRayOriginEXT(const RayQueryEXT& rayQuery) { (void)rayQuery; return {}; }
    Vec3 RayQueryGetWorldRayDirectionEXT(const RayQueryEXT& rayQuery) { (void)rayQuery; return {}; }
    float RayQueryGetIntersectionTEXT(const RayQueryEXT& rayQuery, bool committed) { (void)rayQuery; (void)committed; return 0.0f; }
    int32_t RayQueryGetIntersectionInstanceCustomIndexEXT(const RayQueryEXT& rayQuery, bool committed) { (void)rayQuery; (void)committed; return 0; }
    int32_t RayQueryGetIntersectionInstanceIdEXT(const RayQueryEXT& rayQuery, bool committed) { (void)rayQuery; (void)committed; return 0; }
    uint32_t RayQueryGetIntersectionInstanceShaderBindingTableRecordOffsetEXT(const RayQueryEXT& rayQuery, bool committed) { (void)rayQuery; (void)committed; return 0; }
    int32_t RayQueryGetIntersectionGeometryIndexEXT(const RayQueryEXT& rayQuery, bool committed) { (void)rayQuery; (void)committed; return 0; }
    int32_t RayQueryGetIntersectionPrimitiveIndexEXT(const RayQueryEXT& rayQuery, bool committed) { (void)rayQuery; (void)committed; return 0; }
    Vec2 RayQueryGetIntersectionBarycentricsEXT(const RayQueryEXT& rayQuery, bool committed) { (void)rayQuery; (void)committed; return {}; }
    bool RayQueryGetIntersectionFrontFaceEXT(const RayQueryEXT& rayQuery, bool committed) { (void)rayQuery; (void)committed; return false; }
    bool RayQueryGetIntersectionCandidateAABBOpaqueEXT(const RayQueryEXT& rayQuery) { (void)rayQuery; return false; }
    Vec3 RayQueryGetIntersectionObjectRayDirectionEXT(const RayQueryEXT& rayQuery, bool committed) { (void)rayQuery; (void)committed; return {}; }
    Vec3 RayQueryGetIntersectionObjectRayOriginEXT(const RayQueryEXT& rayQuery, bool committed) { (void)rayQuery; (void)committed; return {}; }
    Mat4x3 RayQueryGetIntersectionObjectToWorldEXT(const RayQueryEXT& rayQuery, bool committed) { (void)rayQuery; (void)committed; return {}; }
    Mat4x3 RayQueryGetIntersectionWorldToObjectEXT(const RayQueryEXT& rayQuery, bool committed) { (void)rayQuery; (void)committed; return {}; }
    void RayQueryGetIntersectionTriangleVertexPositionsEXT(const RayQueryEXT& rayQuery, bool committed, Array<Vec3, 3>& positions) {
        (void)rayQuery; (void)committed; (void)positions;
    }
    void RayQueryGetIntersectionTriangleVertexPositionsEXT(const RayQueryEXT& rayQuery, bool committed, Vec3 (&positions)[3]) {
        (void)rayQuery; (void)committed; (void)positions;
    }

    void RayQueryInitializeEXT(RayQueryEXT& rayQuery, const ASViewer& topLevel,
                                                uint32_t rayFlags, uint32_t cullMask,
                                                const Vec3& origin, float tMin,
                                                const Vec3& direction, float tMax) {
        (void)rayQuery; (void)topLevel; (void)rayFlags; (void)cullMask;
        (void)origin; (void)tMin; (void)direction; (void)tMax;
    }
}
