#pragma once

#include "object.hpp"
#include "ogt_vox.h"
#include <vector>
#include <string> 
#include <glm/glm.hpp>
#include <algorithm>

// Структура для описания найденного изолированного фрагмента
struct VoxelIsland {
    glm::ivec3 minBounds = glm::ivec3(10000000);
    glm::ivec3 maxBounds = glm::ivec3(0);
    std::vector<glm::ivec3> voxelCoords;
};



class BvhBuilder {
public:
    std::vector<GpuBvhNode> flatNodes;

    void Build(const std::vector<GpuEntityMeta>& entities);

private:
    int BuildRecursive(const std::vector<GpuEntityMeta>& entities,
        std::vector<int>& indices,
        size_t start,
        size_t end);
};

class VoxelScene {
public:
    std::vector<VoxelObject> objects;
    int selectedObjectIndex = -1;

    // Загрузка
    bool LoadVox(const std::string& path);

    // Разделение объектов
    void SplitObject(size_t objectIndex);
    std::vector<VoxelIsland> FindIslands(const VoxelObject& srcObj);

    // Обновление GPU
    void UpdateAndUploadToGpu();
    void UploadTransforms();
    void UpdateTransforms();
    void DeleteGPUBuffers();

    // Частичное обновление только грязных бриков
    void UpdateDirtyBricks(std::vector<uint32_t>& macroGrid,
        std::vector<Brick>& bricks,
        std::vector<GpuEntityMeta>& metaBuffer);

    // Полная упаковка одного объекта (для новых)
    void PackObjectFull(size_t objIndex,
        std::vector<uint32_t>& macroGrid,
        std::vector<Brick>& bricks,
        std::vector<Material>& materials,
        std::vector<GpuEntityMeta>& metaBuffer);
    

    // Сборка данных
    void CollectMetaData(std::vector<GpuEntityMeta>& metaBuffer);
    void CollectVoxelData(std::vector<uint32_t>& macroGridLenta,
        std::vector<Brick>& brickLenta,
        std::vector<Material>& materialsLenta,
        std::vector<GpuEntityMeta>& metaBuffer);

    // Загрузка на GPU
    void UploadMetaBuffer(const std::vector<GpuEntityMeta>& metaBuffer);
    void UploadMacroGrid(const std::vector<uint32_t>& macroGridLenta);
    void UploadBricks(const std::vector<Brick>& brickLenta);
    void UploadMaterials(const std::vector<Material>& materialsLenta);
    void UploadBVH(const std::vector<GpuBvhNode>& bvhNodes);


    bool RemoveVoxelByRay(glm::vec3 rayOrigin, glm::vec3 rayDir);

    // Кэш и буферы
    std::vector<GpuEntityMeta> gpuMetaCache;
    GLuint metaSSBO = 0;
    GLuint macroLentaSSBO = 0;
    GLuint brickLentaSSBO = 0;
    GLuint materialsSSBO = 0;
    GLuint bvhSSBO = 0;

    size_t lastObjectCount = 0;
};