#pragma once

#include "Voxel.hpp"

class VoxelModifier {
public:
	static bool RemoveVoxelByRay(VoxelMap& voxelMap,glm::mat4 model, glm::vec3 ro, glm::vec3 rd, glm::ivec3& hitVoxelPos);
};