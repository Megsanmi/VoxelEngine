#include "VoxelModifier.hpp"
#include "AABB.hpp"

bool VoxelModifier::RemoveVoxelByRay(VoxelMap& voxelMap,glm::mat4 modelMatrix, glm::vec3 worldRo, glm::vec3 worldRd,glm::ivec3& hitVoxelPos)
{
    // ИСПРАВЛЕНО: Берем финальную матрицу со смещенным пивотом, чтобы CPU и GPU были синхронны
    glm::mat4 invModel = glm::inverse(modelMatrix);
    glm::vec3 ro = glm::vec3(invModel * glm::vec4(worldRo, 1.0f));
    glm::vec3 rd = glm::normalize(glm::vec3(invModel * glm::vec4(worldRd, 0.0f)));

    float tMin, tMax;
    glm::vec3 boxMin(0.0f), boxMax(voxelMap.size.x, voxelMap.size.y, voxelMap.size.z);

    if (!intersectRayAABB(ro, rd, boxMin, boxMax, tMin, tMax)) return false;
    //if (tMin > maxDist) return false;

    float t = std::max(tMin, 0.001f);
    glm::vec3 pos = ro + rd * t;

    glm::ivec3 step(
        rd.x >= 0 ? 1 : -1,
        rd.y >= 0 ? 1 : -1,
        rd.z >= 0 ? 1 : -1
    );

    glm::ivec3 voxelPos = glm::ivec3(glm::floor(pos));
    voxelPos = glm::clamp(voxelPos, glm::ivec3(0), glm::ivec3(voxelMap.size) - 1);

    for (int i = 0; i < 256; i++) {
        if (voxelPos.x < 0 || voxelPos.x >= voxelMap.size.x ||
            voxelPos.y < 0 || voxelPos.y >= voxelMap.size.y ||
            voxelPos.z < 0 || voxelPos.z >= voxelMap.size.z) break;

        Voxel v = voxelMap.GetVoxel(voxelPos.x, voxelPos.y, voxelPos.z);
        if (v.ID != 0) {
            voxelMap.SetVoxel(voxelPos.x, voxelPos.y, voxelPos.z, 0);
            hitVoxelPos = voxelPos;
            return true;
        }

        glm::vec3 nextBoundary = glm::vec3(
            (step.x > 0 ? voxelPos.x + 1 : voxelPos.x),
            (step.y > 0 ? voxelPos.y + 1 : voxelPos.y),
            (step.z > 0 ? voxelPos.z + 1 : voxelPos.z)
        );

        glm::vec3 tToNext = (nextBoundary - pos) / rd;

        if (tToNext.x < tToNext.y && tToNext.x < tToNext.z) {
            voxelPos.x += step.x;
            pos += rd * tToNext.x;
        }
        else if (tToNext.y < tToNext.z) {
            voxelPos.y += step.y;
            pos += rd * tToNext.y;
        }
        else {
            voxelPos.z += step.z;
            pos += rd * tToNext.z;
        }
    }
    return false;
}
