#pragma once

#include <glad/glad.h> 

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp> 
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cmath>
#include <cfloat>
#include <cstring>

#include "Voxel.hpp"
#include "imgui/imgui.h"


class Transform {
public:
    glm::vec3 position{ 0, 0, 0 };
    glm::vec3 rotationEuler{ 0, 0, 0 };
    glm::vec3 scale{ 1, 1, 1 };
    glm::quat qrotation{ 1, 0, 0, 0 };

    glm::vec3 oldposition{ 0, 0, 0 };
    glm::vec3 oldrotationEuler{ 0, 0, 0 };
    glm::vec3 oldscale{ 1, 1, 1 };
    

    bool isDirty = false;

    void Update(float dt) {
        rotationEuler.x = wrapAngle(rotationEuler.x);
        rotationEuler.y = wrapAngle(rotationEuler.y);
        rotationEuler.z = wrapAngle(rotationEuler.z);
        qrotation = glm::normalize(glm::quat(glm::radians(rotationEuler)));

        if (position != oldposition || rotationEuler != oldrotationEuler) {
            isDirty = true;
            oldposition = position;
            oldrotationEuler = rotationEuler;
        }
        else {
            isDirty = false;
        }
    }

    void drawInspector() {
        if (ImGui::CollapsingHeader("Transform")) {
            ImGui::DragFloat3("Position", &position.x, 0.1f);
            if (ImGui::DragFloat3("Rotation", &rotationEuler.x, 0.5f)) {
                qrotation = glm::normalize(glm::quat(glm::radians(rotationEuler)));
            }
            ImGui::DragFloat3("Scale", &scale.x, 0.01f);
        }
    }

    glm::mat4 GetMatrix() const {
        glm::mat4 model(1.0f);
        model = glm::translate(model, position);
        model *= glm::toMat4(qrotation);
        model = glm::scale(model, scale);
        return model;
    }

private:
    static float wrapAngle(float angle) {
        angle = fmodf(angle + 180.0f, 360.0f);
        if (angle < 0) angle += 360.0f;
        return angle - 180.0f;
    }
};

struct alignas(16) GpuEntityMeta {
    glm::mat4 invModelMatrix;
    glm::mat4 modelMatrix;
    glm::vec4 boxMin;
    glm::vec4 boxMax;
    glm::ivec4 sizeInBricks;  // xyz = размер в бриках, w = macroOffset
    glm::ivec4 sizeInVoxels;  // xyz = размер в вокселях, w = matOffset
};

class VoxelObject {
public:
    Transform transform;
    VoxelMap voxelMap;

    // Флаги для отслеживания изменений
    bool isDirty = true;         // Изменился трансформ
    bool voxelsChanged = true;   // Изменилась геометрия (новый объект)
    bool materialsChanged = true;// Изменились материалы (новый объект)
    bool isNew = true;           // Только что создан

    int bricksX = 0;
    int bricksY = 0;
    int bricksZ = 0;

    VoxelObject(glm::uvec3 realSize) {
        voxelMap.size = realSize;
        bricksX = (voxelMap.size.x + 7) >> 3;
        bricksY = (voxelMap.size.y + 7) >> 3;
        bricksZ = (voxelMap.size.z + 7) >> 3;
    }

    void Update(float dt)
    {
        transform.Update(dt);
        isDirty = transform.isDirty;
    }

    uint32_t GetMacroIndex(int bx, int by, int bz) const {
        return static_cast<uint32_t>(bx + bz * bricksX + by * (bricksX * bricksZ));
    }


    bool intersectAABB(glm::vec3 ro, glm::vec3 rd, glm::vec3 bmin, glm::vec3 bmax, float& tMin, float& tMax) {
        glm::vec3 invRd = 1.0f / (rd + glm::vec3(1e-8f));
        glm::vec3 t0 = (bmin - ro) * invRd;
        glm::vec3 t1 = (bmax - ro) * invRd;
        glm::vec3 tmin3 = glm::min(t0, t1);
        glm::vec3 tmax3 = glm::max(t0, t1);
        tMin = glm::max(glm::max(tmin3.x, tmin3.y), tmin3.z);
        tMax = glm::min(glm::min(tmax3.x, tmax3.y), tmax3.z);
        return tMin <= tMax && tMax > 0.0f;
    }

    bool RemoveVoxelByRay(glm::vec3 worldRo, glm::vec3 worldRd, float maxDist = 1000.0f) {
        // Трансформируем луч в локальное пространство
        glm::mat4 invModel = glm::inverse(transform.GetMatrix());
        glm::vec3 ro = glm::vec3(invModel * glm::vec4(worldRo, 1.0f));
        glm::vec3 rd = glm::normalize(glm::vec3(invModel * glm::vec4(worldRd, 0.0f)));

        float tMin, tMax;
        glm::vec3 boxMin(0.0f), boxMax(voxelMap.size.x, voxelMap.size.y, voxelMap.size.z);

        // Пересечение с AABB объекта
        if (!intersectAABB(ro, rd, boxMin, boxMax, tMin, tMax)) return false;
        if (tMin > maxDist) return false;

        float t = std::max(tMin, 0.0f);
        glm::vec3 pos = ro + rd * (t + 0.001f);

        // Простой DDA для поиска вокселя
        glm::ivec3 step(
            rd.x >= 0 ? 1 : -1,
            rd.y >= 0 ? 1 : -1,
            rd.z >= 0 ? 1 : -1
        );

        glm::ivec3 voxelPos = glm::ivec3(glm::floor(pos));
        voxelPos = glm::clamp(voxelPos, glm::ivec3(0), glm::ivec3(voxelMap.size) - 1);

        // Проверяем ближайшие воксели вдоль луча
        for (int i = 0; i < 256; i++) {
            if (voxelPos.x < 0 || voxelPos.x >= voxelMap.size.x ||
                voxelPos.y < 0 || voxelPos.y >= voxelMap.size.y ||
                voxelPos.z < 0 || voxelPos.z >= voxelMap.size.z) break;

            Voxel v = voxelMap.GetVoxel(voxelPos.x, voxelPos.y, voxelPos.z);
            if (v.ID != 0) {
                voxelMap.SetVoxel(voxelPos.x, voxelPos.y, voxelPos.z, 0);
                MarkVoxelsChanged();
                return true;
            }

            // DDA шаг
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

    void PackObjectData(std::vector<uint32_t>& outMacroGrid, std::vector<Brick>& outBricks) const {
        std::vector<uint32_t> localGrid(bricksX * bricksY * bricksZ, 0);

        for (int bx = 0; bx < bricksX; ++bx) {
            for (int by = 0; by < bricksY; ++by) {
                for (int bz = 0; bz < bricksZ; ++bz) {

                    Brick tempBrick = {};
                    bool hasGeometry = false;

                    for (int vy = 0; vy < 8; ++vy) {
                        for (int vz = 0; vz < 8; ++vz) {
                            for (int vx = 0; vx < 8; ++vx) {
                                int gx = (bx << 3) + vx;
                                int gy = (by << 3) + vy;
                                int gz = (bz << 3) + vz;

                                if (gx >= voxelMap.size.x || gy >= voxelMap.size.y || gz >= voxelMap.size.z)
                                    continue;

                                Voxel v = voxelMap.GetVoxel(gx, gy, gz);
                                if (v.ID != 0) {
                                    uint16_t idx = Brick::GetLinearIndex(vx, vy, vz);
                                    tempBrick.Data[idx].ID = v.ID;
                                    hasGeometry = true;
                                }
                            }
                        }
                    }

                    if (hasGeometry) {
                        uint32_t globalBrickId = static_cast<uint32_t>(outBricks.size());
                        outBricks.push_back(tempBrick);
                        localGrid[GetMacroIndex(bx, by, bz)] = globalBrickId;
                    }
                }
            }
        }

        outMacroGrid.insert(outMacroGrid.end(), localGrid.begin(), localGrid.end());
    }

    void UpdateBoundsAndMatrices(GpuEntityMeta& outMeta) {
        outMeta.modelMatrix = transform.GetMatrix();
        outMeta.invModelMatrix = glm::inverse(outMeta.modelMatrix);

        glm::vec3 localMin(0.0f);
        glm::vec3 localMax(voxelMap.size.x, voxelMap.size.y, voxelMap.size.z);

        glm::vec3 corners[8] = {
            {localMin.x, localMin.y, localMin.z}, {localMax.x, localMin.y, localMin.z},
            {localMin.x, localMax.y, localMin.z}, {localMax.x, localMax.y, localMin.z},
            {localMin.x, localMin.y, localMax.z}, {localMax.x, localMin.y, localMax.z},
            {localMin.x, localMax.y, localMax.z}, {localMax.x, localMax.y, localMax.z}
        };

        glm::vec3 worldMin(FLT_MAX), worldMax(-FLT_MAX);
        for (int i = 0; i < 8; i++) {
            glm::vec3 worldCorner = glm::vec3(outMeta.modelMatrix * glm::vec4(corners[i], 1.0f));
            worldMin = glm::min(worldMin, worldCorner);
            worldMax = glm::max(worldMax, worldCorner);
        }

        outMeta.boxMin = glm::vec4(worldMin, 0.0f);
        outMeta.boxMax = glm::vec4(worldMax, 0.0f);
        outMeta.sizeInBricks = glm::ivec4(bricksX, bricksY, bricksZ, 0);
        outMeta.sizeInVoxels = glm::ivec4(voxelMap.size.x, voxelMap.size.y, voxelMap.size.z, 0);

        isDirty = false;
        transform.isDirty = false;
    }

    // Метод для отметки изменений вокселей
    void MarkVoxelsChanged() {
        voxelsChanged = true;
        isDirty = true;
    }

    // Метод для отметки изменений материалов
    void MarkMaterialsChanged() {
        materialsChanged = true;
        isDirty = true;
    }
    
    
};

struct MemoryBlock {
    size_t offset;
    size_t size;
};

class GpuPoolManager {
public:
    size_t totalSize;
    std::vector<MemoryBlock> freeBlocks; // Список пустых дыр в памяти

    GpuPoolManager(size_t maxSize) {
        totalSize = maxSize;
        freeBlocks.push_back({ 0, maxSize }); // Изначально вся память свободна
    }

    // Возвращает смещение (offset) в GPU буфере
    size_t Allocate(size_t size) {
        for (auto it = freeBlocks.begin(); it != freeBlocks.end(); ++it) {
            if (it->size >= size) {
                size_t allocatedOffset = it->offset;
                if (it->size == size) {
                    freeBlocks.erase(it);
                }
                else {
                    it->offset += size;
                    it->size -= size;
                }
                return allocatedOffset;
            }
        }
    }

    void Free(size_t offset, size_t size) {
        freeBlocks.push_back({ offset, size });
        // Тут можно сделать дефрагментацию (слияние соседних пустых блоков)
    }
};
