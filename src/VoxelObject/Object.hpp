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
#include <algorithm>

#include "Voxel.hpp"
#include "imgui/imgui.h"
#include "../AABB.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
enum class VoxelObjectType : uint32_t {
    DunamicObject = 0, // Обычный объект (показываем в инспекторах, крутим, физика)
    WorldChunk = 1  // Чанк мира (скрытый, статичный)
};

class Transform {
public:
    glm::vec3 position{ 0, 0, 0 };
    glm::vec3 rotationEuler{ 0, 0, 0 };
    glm::vec3 scale{ 0.1, 0.1, 0.1 };
    glm::quat qrotation{ 1, 0, 0, 0 };

    glm::vec3 oldposition{ 0, 0, 0 };
    glm::vec3 oldrotationEuler{ 0, 0, 0 };
    glm::vec3 oldscale{ 0.1, 0.1, 0.1 };

    glm::vec3 offsetCollision = { 0, 0, 0 };
    bool isDirty = false;

    void Update(float dt) {
        rotationEuler.x = wrapAngle(rotationEuler.x);
        rotationEuler.y = wrapAngle(rotationEuler.y);
        rotationEuler.z = wrapAngle(rotationEuler.z);
        qrotation = glm::normalize(glm::quat(glm::radians(rotationEuler)));

        if (position != oldposition || rotationEuler != oldrotationEuler || scale != oldscale) {
            isDirty = true;
            oldposition = position;
            oldrotationEuler = rotationEuler;
            oldscale = scale;
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

class Chunk
{
public:
    glm::ivec3 pos = glm::ivec3(0);
    uint32_t macroOffset = 0;
    uint32_t id = 0;
   
    JPH::BodyID bodyID;
    glm::vec3 offsetCollision = {0, 0, 0};

    static constexpr int CHUNK_SIZE = 64;

    int bricksX = CHUNK_SIZE/8;
    int bricksY = CHUNK_SIZE/8;
    int bricksZ = CHUNK_SIZE/8;
    
    VoxelMap voxelMap;

    // Оператор перемещающего присваивания (ЕГО СЕЙЧАС НЕ ХВАТАЕТ)
    Chunk& operator=(Chunk&& other) noexcept = default;

    // Явно запрещаем копирование, чтобы избежать случайных тяжелых аллокаций
    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;

    Chunk() {
        voxelMap.size = glm::vec3(CHUNK_SIZE);
        

    }
    uint32_t GetMacroIndex(int bx, int by, int bz) const {
        return static_cast<uint32_t>(bx + bz * bricksX + by * (bricksX * bricksZ));
    }
};

struct alignas(16) GpuChunkMeta {
    glm::ivec4 pos;  // xyz = позиция, w = macroOffset
    uint32_t BVHnode;
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
    Material materials[256];


    uint32_t id = 0;

    JPH::BodyID bodyID;

    unsigned int macroGridOffset = 0;
    unsigned int matOffset = 0;


    glm::vec3 min = glm::vec3(FLT_MAX);     
    glm::vec3 max = glm::vec3(-FLT_MAX);

    bool isDirty = true;
    bool voxelsChanged = true;
    bool materialsChanged = true;
    bool isNew = true;

    int bricksX = 0;
    int bricksY = 0;
    int bricksZ = 0;

    VoxelObject(glm::uvec3 realSize) {
        voxelMap.size = realSize;
        bricksX = (voxelMap.size.x + 7) >> 3;
        bricksY = (voxelMap.size.y + 7) >> 3;
        bricksZ = (voxelMap.size.z + 7) >> 3;
    }

    VoxelObject() : bricksX(0), bricksY(0), bricksZ(0) {
        voxelMap.size = glm::uvec3(0);
    }

    void Update(float dt) {
        transform.Update(dt);
        if (transform.isDirty) {
            isDirty = true;
        }
        CalculateWorldAABB();
    }

    unsigned int GetMacroGridSize() {
        return bricksX * bricksY * bricksZ;
    }

    uint32_t GetMacroIndex(int bx, int by, int bz) const {
        return static_cast<uint32_t>(bx + bz * bricksX + by * (bricksX * bricksZ));
    }

    
    void CalculateWorldAABB() {
        // Локальные границы воксельного объекта (от 0 до размера в вокселях)
        glm::vec3 localMin(0.0f);
        glm::vec3 localMax(static_cast<float>(voxelMap.size.x),
            static_cast<float>(voxelMap.size.y),
            static_cast<float>(voxelMap.size.z));

        // Массив из 8 вершин локального куба объекта
        glm::vec3 vertices[8] = {
            glm::vec3(localMin.x, localMin.y, localMin.z),
            glm::vec3(localMax.x, localMin.y, localMin.z),
            glm::vec3(localMin.x, localMax.y, localMin.z),
            glm::vec3(localMax.x, localMax.y, localMin.z),
            glm::vec3(localMin.x, localMin.y, localMax.z),
            glm::vec3(localMax.x, localMin.y, localMax.z),
            glm::vec3(localMin.x, localMax.y, localMax.z),
            glm::vec3(localMax.x, localMax.y, localMax.z)
        };

        // Получаем правильную финальную матрицу (с учетом смещения пивота для мебели)
        glm::mat4 modelMatrix = GetFinalModelMatrix();

        // Инициализируем выходные границы экстремальными значениями
        min = glm::vec3(FLT_MAX);
        max = glm::vec3(-FLT_MAX);

        // Трансформируем каждую из 8 вершин в мировые координаты и обновляем границы
        for (int i = 0; i < 8; ++i) {
            glm::vec4 worldPos = modelMatrix * glm::vec4(vertices[i], 1.0f);

            min = glm::min(min, glm::vec3(worldPos));
            max = glm::max(max, glm::vec3(worldPos));
        }
    }


    // ВАЖНО: Хелпер для получения корректной финальной модельной матрицы с учетом пивота
    glm::mat4 GetFinalModelMatrix() const {
        glm::mat4 baseModel = transform.GetMatrix();

        glm::vec3 centerOffset = glm::vec3(voxelMap.size) * 0.5f;
        return glm::translate(baseModel, -centerOffset);
            
    }

    bool RemoveVoxelByRay(glm::vec3 worldRo, glm::vec3 worldRd, glm::ivec3& outHitPos, float maxDist = 1000.0f) {
        // ИСПРАВЛЕНО: Берем финальную матрицу со смещенным пивотом, чтобы CPU и GPU были синхронны
        glm::mat4 invModel = glm::inverse(GetFinalModelMatrix());
        glm::vec3 ro = glm::vec3(invModel * glm::vec4(worldRo, 1.0f));
        glm::vec3 rd = glm::normalize(glm::vec3(invModel * glm::vec4(worldRd, 0.0f)));

        float tMin, tMax;
        glm::vec3 boxMin(0.0f), boxMax(voxelMap.size.x, voxelMap.size.y, voxelMap.size.z);

        if (!intersectRayAABB(ro, rd, boxMin, boxMax, tMin, tMax)) return false;
        if (tMin > maxDist) return false;

        float t = std::max(tMin, 0.0f);
        glm::vec3 pos = ro + rd * (t + 0.001f);

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
                outHitPos = voxelPos;

                MarkVoxelsChanged();
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

    void UpdateBoundsAndMatrices(GpuEntityMeta& outMeta) {
        // ИСПРАВЛЕНО: Используем единую функцию рассчета финальной матрицы
        glm::mat4 finalModel = GetFinalModelMatrix();

        outMeta.modelMatrix = finalModel;
        outMeta.invModelMatrix = glm::inverse(finalModel);

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
            glm::vec3 worldCorner = glm::vec3(finalModel * glm::vec4(corners[i], 1.0f));
            worldMin = glm::min(worldMin, worldCorner);
            worldMax = glm::max(worldMax, worldCorner);
        }

        // ДОПИСАНО: Корректно закрываем расчет AABB для GPU BVH дерева
        outMeta.boxMin = glm::vec4(worldMin, 0.0f);
        outMeta.boxMax = glm::vec4(worldMax, 0.0f);

        outMeta.sizeInBricks.x = bricksX;
        outMeta.sizeInBricks.y = bricksY;
        outMeta.sizeInBricks.z = bricksZ;

        outMeta.sizeInVoxels.x = voxelMap.size.x;
        outMeta.sizeInVoxels.y = voxelMap.size.y;
        outMeta.sizeInVoxels.z = voxelMap.size.z;

        isDirty = false;
        transform.isDirty = false;
    }

    void MarkVoxelsChanged() {
        voxelsChanged = true;
        isDirty = true;
    }

    void MarkMaterialsChanged() {
        materialsChanged = true;
        isDirty = true;
    }
};
