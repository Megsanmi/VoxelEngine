#pragma once

#include "object.hpp"
#include "ogt_vox.h"
#include <vector>
#include <string> 
#include <glm/glm.hpp>
#include <algorithm>
#include "Manager.hpp"
#include "renderer/camera.hpp"
#include <unordered_map>
#include "StaticGrid.hpp"

// Структура для описания найденного изолированного фрагмента
struct VoxelIsland {
    glm::ivec3 minBounds = glm::ivec3(10000000);
    glm::ivec3 maxBounds = glm::ivec3(0);
    std::vector<glm::ivec3> voxelCoords;
};


class VoxelScene {
public:
    VoxelManager manager;
    Camera camera;

    std::vector<uint32_t> objectIDs;
    int selectedObjectIndex = -1;


    VoxelScene() : manager(100000) {}

    void Init();

    void Update(float dt);

    // Высокоуровневые операции (Управление объектами)
    uint32_t LoadVox(const std::string& path);
    void UnregisterObject(uint32_t objId)
    {
        manager.UnregisterObject(objId);
    }; 

    VoxelObject& GetObject(uint32_t objId) {
        return manager.GetObject(objId);
    }

    void SplitObject(uint32_t objectIndex);
    bool RemoveVoxelByRay(glm::vec3 rayOrigin, glm::vec3 rayDir);
    std::vector<VoxelIsland> FindIslands(const VoxelObject& srcObj);


    // 4. КОМПОНЕНТЫ СИСТЕМЫ РЕНДЕРИНГА
    // Здесь остаются ТОЛЬКО те буферы, которые собираются со всей сцены глобально каждый кадр
    std::vector<GpuEntityMeta> gpuMetaCache; // Кэш матриц и AABB для отправки на GPU
    GLuint metaSSBO = 0;                     // Буфер метаданных всех объектов сцены
    GLuint bvhSSBO = 0;                      // Буфер глобального BVH-дерева для Raymarching шейдера

    size_t lastObjectCount = 0;
};
