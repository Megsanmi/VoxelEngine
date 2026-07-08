#pragma once

#include "../VoxelObject/object.hpp"
#include "ogt_vox.h"
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <algorithm>
#include "Manager.hpp"
#include "../renderer/camera.hpp"
#include <unordered_map>
#include "../StaticGrid.hpp"
#include "../Player/Player.hpp"

struct VoxelIsland
{
    glm::ivec3 minBounds = glm::ivec3(10000000);
    glm::ivec3 maxBounds = glm::ivec3(0);
    std::vector<glm::ivec3> voxelCoords;
};

class VoxelScene
{
public:
    VoxelManager manager;
    Camera camera;
    InputController controller;

    std::vector<uint32_t> objectIDs;
    int selectedObjectIndex = -1;

    std::vector<GpuEntityMeta> gpuMetaCache;
    GLuint metaSSBO = 0;
    GLuint bvhSSBO = 0;

    size_t lastObjectCount = 0;

    VoxelScene() : manager(500000) {}

    void Init();
    void Update(float dt);

    uint32_t LoadVox(const std::string& path, glm::vec3 pos = glm::vec3(0));

    void UnregisterObject(uint32_t objId)
    {
        manager.UnregisterObject(objId);
    }

    VoxelObject& GetObject(uint32_t objId)
    {
        return manager.GetObject(objId);
    }

    void SplitObject(uint32_t objectIndex);
    bool RemoveVoxelByRay(glm::vec3 rayOrigin, glm::vec3 rayDir);
    void HandleMouseClick(glm::vec3 rayOrigin, glm::vec3 rayDir);
    void HandleMouseMove(glm::vec3 rayOrigin, glm::vec3 rayDir);
    void HandleMouseRelease();

    std::vector<VoxelIsland> FindIslands(const VoxelObject& srcObj);

    void ClearScene();

};