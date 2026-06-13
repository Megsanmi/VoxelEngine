#include "scene.hpp"
#include <queue>
#include <fstream>
#include <iostream>
#include <cstring>
#include <algorithm>

// ==========================================
// ЗАГРУЗКА VOX ФАЙЛОВ
// ==========================================
bool VoxelScene::LoadVox(const std::string& path)
{
    // Обязательно очищаем старые объекты, чтобы не было наложения данных
    //objects.clear();

    std::ifstream file(path, std::ios::binary);
    if (!file) return false;

    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(fileSize);
    file.read((char*)buffer.data(), fileSize);

    const ogt_vox_scene* scene =
        ogt_vox_read_scene(buffer.data(), (uint32_t)buffer.size());

    if (!scene) return false;

    for (uint32_t i = 0; i < 1; i++)
    {
        const auto& instance = scene->instances[i];
        const auto* model = scene->models[instance.model_index];

        glm::ivec3 mapMin(INT_MAX);
        glm::ivec3 mapMax(INT_MIN);
        bool hasVoxel = false;

        // 1. Ищем bounds в оригинальном пространстве модели
        for (int z = 0; z < model->size_z; z++)
        {
            for (int y = 0; y < model->size_y; y++)
            {
                for (int x = 0; x < model->size_x; x++)
                {
                    int idx = x + y * model->size_x + z * model->size_x * model->size_y;
                    if (model->voxel_data[idx] == 0) continue;

                    hasVoxel = true;
                    glm::ivec3 currentPos(x, z, y); // Y-up поворот
                    mapMin = glm::min(mapMin, currentPos);
                    mapMax = glm::max(mapMax, currentPos);
                }
            }
        }

        if (!hasVoxel) continue;

        glm::ivec3 size = mapMax - mapMin + glm::ivec3(1);

        // Защита от битой памяти
        if (size.x <= 0 || size.y <= 0 || size.z <= 0 || size.x > 512 || size.y > 512 || size.z > 512) continue;

        VoxelObject obj(size);

        // 2. Расчет правильной трансформации с компенсацией Pivot
        // Берем базовую позицию из файла (меняя Y и Z местами)
        glm::vec3 basePosition = glm::vec3(
            instance.transform.m30,
            instance.transform.m32,
            instance.transform.m31
        );

        obj.transform.scale = glm::vec3(1.0f);

        // Строим матрицу вращения
        glm::vec3 c0(instance.transform.m00, instance.transform.m02, instance.transform.m01);
        glm::vec3 c1(instance.transform.m20, instance.transform.m22, instance.transform.m21);
        glm::vec3 c2(instance.transform.m10, instance.transform.m12, instance.transform.m11);

        glm::mat3 rot(1.0f);
        rot[0] = glm::normalize(c0);
        rot[1] = glm::normalize(c1);
        rot[2] = glm::normalize(c2);

        obj.transform.qrotation = glm::quat_cast(rot);
        obj.transform.rotationEuler = glm::degrees(glm::eulerAngles(obj.transform.qrotation));

        // ВАЖНО: Компенсируем смещение mapMin через матрицу вращения.
        // Переводим смещение bounds в мировые координаты инстанса, чтобы деталь не улетала.
        glm::vec3 pivotOffset = glm::vec3(mapMin);
        glm::vec3 worldOffset = rot * pivotOffset;

        // Итоговая позиция инстанса в мире
        obj.transform.position = basePosition + glm::vec3(mapMin) + glm::vec3(size) * 0.5f;
        obj.isDirty = true;

        // 3. Заполнение палитры
        for (int p = 0; p < 256; p++)
        {
            ogt_vox_rgba c = scene->palette.color[p];
            Material m;
            m.Color[0] = c.r;
            m.Color[1] = c.g;
            m.Color[2] = c.b;
            obj.voxelMap.materials[p] = m;
        }

        // 4. Копирование вокселей локально (от 0 до size)
        for (int z = 0; z < model->size_z; z++)
        {
            for (int y = 0; y < model->size_y; y++)
            {
                for (int x = 0; x < model->size_x; x++)
                {
                    int idx = x + y * model->size_x + z * model->size_x * model->size_y;
                    uint8_t id = model->voxel_data[idx];

                    if (id == 0) continue;

                    // Локальная позиция внутри сжатого VoxelObject
                    glm::ivec3 p = glm::ivec3(x, z, y) - mapMin;

                    obj.voxelMap.SetVoxel(p.x, p.y, p.z, id);
                }
            }
        }

        objects.push_back(std::move(obj));
    }

    ogt_vox_destroy_scene(scene);
    return true;
}

// ==========================================
// РАЗДЕЛЕНИЕ ОБЪЕКТОВ НА ОСТРОВА
// ==========================================

std::vector<VoxelIsland> VoxelScene::FindIslands(const VoxelObject& srcObj)
{
    glm::ivec3 size = srcObj.voxelMap.size;
    size_t totalVoxels = static_cast<size_t>(size.x) * size.y * size.z;

    std::vector<bool> visited(totalVoxels, false);
    std::vector<VoxelIsland> islands;

    const int dx[] = { 1, -1, 0, 0, 0, 0 };
    const int dy[] = { 0, 0, 1, -1, 0, 0 };
    const int dz[] = { 0, 0, 0, 0, 1, -1 };

    auto getIndex = [&](int x, int y, int z) -> size_t {
        return x + z * size.x + y * size.x * size.z;
        };

    for (int y = 0; y < size.y; ++y)
    {
        for (int z = 0; z < size.z; ++z)
        {
            for (int x = 0; x < size.x; ++x)
            {
                size_t idx = getIndex(x, y, z);

                if (visited[idx]) continue;

                Voxel v = srcObj.voxelMap.GetVoxel(x, y, z);
                if (v.ID == 0) {
                    visited[idx] = true;
                    continue;
                }

                // Нашли новый остров
                VoxelIsland newIsland;
                std::queue<glm::ivec3> q;

                q.push({ x, y, z });
                visited[idx] = true;

                while (!q.empty())
                {
                    glm::ivec3 curr = q.front();
                    q.pop();

                    newIsland.voxelCoords.push_back(curr);
                    newIsland.minBounds = glm::min(newIsland.minBounds, curr);
                    newIsland.maxBounds = glm::max(newIsland.maxBounds, curr);

                    for (int i = 0; i < 6; ++i)
                    {
                        int nx = curr.x + dx[i];
                        int ny = curr.y + dy[i];
                        int nz = curr.z + dz[i];

                        if (nx >= 0 && nx < size.x &&
                            ny >= 0 && ny < size.y &&
                            nz >= 0 && nz < size.z)
                        {
                            size_t nIdx = getIndex(nx, ny, nz);

                            if (!visited[nIdx]) {
                                Voxel nVoxel = srcObj.voxelMap.GetVoxel(nx, ny, nz);
                                if (nVoxel.ID != 0) {
                                    visited[nIdx] = true;
                                    q.push({ nx, ny, nz });
                                }
                            }
                        }
                    }
                }

                islands.push_back(newIsland);
            }
        }
    }

    return islands;
}

void VoxelScene::SplitObject(size_t objectIndex)
{
    if (objectIndex >= objects.size()) return;

    VoxelObject& srcObj = objects[objectIndex];
    std::vector<VoxelIsland> islands = FindIslands(srcObj);

    if (islands.size() <= 1) {
        
        return;
    }

    // Копируем оригинал ДО любых изменений вектора
    VoxelObject originalObj = srcObj; // Полная копия

    // Создаём новые объекты
    std::vector<VoxelObject> newObjects;
    newObjects.reserve(islands.size());

    for (const auto& island : islands)
    {
        glm::ivec3 minB = island.minBounds;
        glm::ivec3 maxB = island.maxBounds;

        glm::uvec3 newSize(
            maxB.x - minB.x + 1,
            maxB.y - minB.y + 1,
            maxB.z - minB.z + 1
        );

        VoxelObject newObj(newSize);

        // Копируем материалы
        std::memcpy(newObj.voxelMap.materials,
            originalObj.voxelMap.materials,
            sizeof(originalObj.voxelMap.materials));

        // Копируем ВЕСЬ трансформ
        newObj.transform.position = originalObj.transform.position;
        newObj.transform.rotationEuler = originalObj.transform.rotationEuler;
        newObj.transform.qrotation = originalObj.transform.qrotation;
        newObj.transform.scale = originalObj.transform.scale;

        // Смещаем позицию для этого острова
        newObj.transform.position += glm::vec3(minB) * originalObj.transform.scale;

        // Переносим воксели
        for (const auto& coord : island.voxelCoords)
        {
            Voxel v = originalObj.voxelMap.GetVoxel(coord.x, coord.y, coord.z);

            int localX = coord.x - minB.x;
            int localY = coord.y - minB.y;
            int localZ = coord.z - minB.z;

            if (localX >= 0 && localX < (int)newSize.x &&
                localY >= 0 && localY < (int)newSize.y &&
                localZ >= 0 && localZ < (int)newSize.z)
            {
                newObj.voxelMap.SetVoxel(localX, localY, localZ, v.ID);
            }
        }

        newObj.isDirty = true;
        newObj.isNew = true;
        newObjects.push_back(std::move(newObj));
    }

    // Заменяем оригинал первым островом
    objects[objectIndex] = std::move(newObjects[0]);

    // Добавляем остальные
    for (size_t i = 1; i < newObjects.size(); ++i) {
        objects.push_back(std::move(newObjects[i]));
    }

    std::cout << "Split complete. Total objects: " << objects.size() << std::endl;
}
// ==========================================
// СБОРКА ДАННЫХ ДЛЯ GPU
// ==========================================

void VoxelScene::CollectMetaData(std::vector<GpuEntityMeta>& metaBuffer)
{
    metaBuffer.clear();
    metaBuffer.reserve(objects.size());

    for (auto& obj : objects)
    {
        glm::mat4 model = obj.transform.GetMatrix();
        glm::vec3 voxelSize = glm::vec3(obj.voxelMap.size);
        glm::vec3 worldSize = voxelSize * obj.transform.scale;

        GpuEntityMeta meta;
        meta.modelMatrix = model;
        meta.invModelMatrix = glm::inverse(model);
        meta.boxMin = glm::vec4(obj.transform.position, 1.0f);
        meta.boxMax = glm::vec4(obj.transform.position + worldSize, 1.0f);
        meta.sizeInBricks = glm::ivec4(obj.bricksX, obj.bricksY, obj.bricksZ, 0);
        meta.sizeInVoxels = glm::ivec4(obj.voxelMap.size.x, obj.voxelMap.size.y, obj.voxelMap.size.z, 0);

        metaBuffer.push_back(meta);
    }
}

void VoxelScene::CollectVoxelData(std::vector<uint32_t>& macroGridLenta,
    std::vector<Brick>& brickLenta,
    std::vector<Material>& materialsLenta,
    std::vector<GpuEntityMeta>& metaBuffer)
{
    macroGridLenta.clear();
    brickLenta.clear();
    materialsLenta.clear();

    // Пустой брик
    brickLenta.push_back(Brick());
    
    for (size_t i = 0; i < objects.size(); i++)
    {
        auto& obj = objects[i];

        // Сохраняем смещения
        metaBuffer[i].sizeInBricks.w = static_cast<int>(macroGridLenta.size());
        metaBuffer[i].sizeInVoxels.w = static_cast<int>(materialsLenta.size());

        // Упаковываем данные
        obj.PackObjectData(macroGridLenta, brickLenta);

        // Копируем материалы
        materialsLenta.insert(materialsLenta.end(),
            std::begin(obj.voxelMap.materials),
            std::end(obj.voxelMap.materials));
    }
    std::cout << brickLenta.size() << std::endl;
}

// ==========================================
// ЗАГРУЗКА НА GPU
// ==========================================

void VoxelScene::UploadMetaBuffer(const std::vector<GpuEntityMeta>& metaBuffer)
{
    if (metaSSBO == 0) glGenBuffers(1, &metaSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, metaSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
        metaBuffer.size() * sizeof(GpuEntityMeta),
        metaBuffer.data(),
        GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, metaSSBO);

    // Сохраняем копию для быстрого обновления трансформов
    gpuMetaCache = metaBuffer;
}

void VoxelScene::UploadMacroGrid(const std::vector<uint32_t>& macroGridLenta)
{
    if (macroLentaSSBO == 0) glGenBuffers(1, &macroLentaSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, macroLentaSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
        macroGridLenta.size() * sizeof(uint32_t),
        macroGridLenta.data(),
        GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, macroLentaSSBO);
}

void VoxelScene::UploadBricks(const std::vector<Brick>& brickLenta)
{
    if (brickLentaSSBO == 0) glGenBuffers(1, &brickLentaSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, brickLentaSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
        brickLenta.size() * sizeof(Brick),
        brickLenta.data(),
        GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, brickLentaSSBO);
}

void VoxelScene::UploadMaterials(const std::vector<Material>& materialsLenta)
{
    if (materialsSSBO == 0) glGenBuffers(1, &materialsSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, materialsSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
        materialsLenta.size() * sizeof(Material),
        materialsLenta.data(),
        GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, materialsSSBO);
}

void VoxelScene::UploadBVH(const std::vector<GpuBvhNode>& bvhNodes)
{
    if (bvhSSBO == 0) glGenBuffers(1, &bvhSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, bvhSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
        bvhNodes.size() * sizeof(GpuBvhNode),
        bvhNodes.data(),
        GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, bvhSSBO);
}

bool VoxelScene::RemoveVoxelByRay(glm::vec3 rayOrigin, glm::vec3 rayDir)
{
    // Сортируем объекты по расстоянию до камеры
    std::vector<std::pair<float, size_t>> sortedObjects;
    for (size_t i = 0; i < objects.size(); i++) {
        glm::vec3 objCenter = objects[i].transform.position;
        float dist = glm::length(objCenter - rayOrigin);
        sortedObjects.push_back({ dist, i });
    }
    std::sort(sortedObjects.begin(), sortedObjects.end());

    // Проверяем объекты от ближнего к дальнему
    for (auto& [dist, idx] : sortedObjects) {
        if (objects[idx].RemoveVoxelByRay(rayOrigin, rayDir)) {
            return true;
        }
    }
    return false;
}

void VoxelScene::DeleteGPUBuffers()
{
    if (metaSSBO) { glDeleteBuffers(1, &metaSSBO); metaSSBO = 0; }
    if (macroLentaSSBO) { glDeleteBuffers(1, &macroLentaSSBO); macroLentaSSBO = 0; }
    if (brickLentaSSBO) { glDeleteBuffers(1, &brickLentaSSBO); brickLentaSSBO = 0; }
    if (materialsSSBO) { glDeleteBuffers(1, &materialsSSBO); materialsSSBO = 0; }
    if (bvhSSBO) { glDeleteBuffers(1, &bvhSSBO); bvhSSBO = 0; }
}

// ==========================================
// ГЛАВНЫЕ МЕТОДЫ ОБНОВЛЕНИЯ
// ==========================================
void VoxelScene::UpdateAndUploadToGpu()
{
    if (objects.empty()) return;

    bool needsFullRebuild = false;

    for (auto& obj : objects) {
        obj.Update(0.016f);

        if (obj.voxelsChanged || obj.materialsChanged || obj.isNew) {
            needsFullRebuild = true;
            obj.voxelsChanged = false;
            obj.materialsChanged = false;
            obj.isNew = false;
        }
    }

    if (objects.size() != lastObjectCount) {
        needsFullRebuild = true;
        lastObjectCount = objects.size();
    }

    // Сначала собираем МЕТАДАННЫЕ
    std::vector<GpuEntityMeta> metaBuffer;
    CollectMetaData(metaBuffer);

    // Обновляем bounds и матрицы ДЛЯ ВСЕХ объектов
    for (size_t i = 0; i < objects.size(); i++) {
        objects[i].UpdateBoundsAndMatrices(metaBuffer[i]);
    }

    if (needsFullRebuild) {
        // Собираем воксельные данные ПОСЛЕ обновления метаданных
        std::vector<uint32_t> macroGridLenta;
        std::vector<Brick> brickLenta;
        std::vector<Material> materialsLenta;
        CollectVoxelData(macroGridLenta, brickLenta, materialsLenta, metaBuffer);

        UploadMacroGrid(macroGridLenta);
        UploadBricks(brickLenta);
        UploadMaterials(materialsLenta);
    }

    // Всегда обновляем метаданные
    UploadMetaBuffer(metaBuffer);

    // Строим BVH ПОСЛЕ того как все boxMin/boxMax обновлены
    BvhBuilder bvh;
    bvh.Build(metaBuffer);
    UploadBVH(bvh.flatNodes);

    gpuMetaCache = metaBuffer;

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}
void VoxelScene::UploadTransforms()
{
    if (gpuMetaCache.size() != objects.size()) {
        UpdateAndUploadToGpu();
        return;
    }

    bool anyDirty = false;

    for (size_t i = 0; i < objects.size(); i++)
    {
        if (objects[i].isDirty || objects[i].transform.isDirty)
        {
            objects[i].UpdateBoundsAndMatrices(gpuMetaCache[i]);
            anyDirty = true;
        }
    }

    if (anyDirty && !gpuMetaCache.empty())
    {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, metaSSBO);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
            gpuMetaCache.size() * sizeof(GpuEntityMeta),
            gpuMetaCache.data());
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }
}

void calculateWorldAABB(const GpuEntityMeta& entity, glm::vec3& outMin, glm::vec3& outMax) {
    outMin = glm::vec3(entity.boxMin);
    outMax = glm::vec3(entity.boxMax);
}

void VoxelScene::UpdateTransforms()
{
    std::vector<GpuEntityMeta> meta(objects.size());

    for (size_t i = 0; i < objects.size(); i++)
    {
        auto& obj = objects[i];

        meta[i].modelMatrix =
            obj.transform.GetMatrix();

        meta[i].invModelMatrix =
            glm::inverse(meta[i].modelMatrix);

        calculateWorldAABB(
            meta[i],
            glm::vec3(meta[i].boxMin),
            glm::vec3(meta[i].boxMax)
        );
    }

    UploadTransforms();
}
// ==========================================
// BVH BUILDER
// ==========================================

void BvhBuilder::Build(const std::vector<GpuEntityMeta>& entities)
{
    flatNodes.clear();
    if (entities.empty()) return;

    flatNodes.reserve(entities.size() * 2);

    std::vector<int> objectIndices(entities.size());
    for (size_t i = 0; i < entities.size(); ++i) {
        objectIndices[i] = static_cast<int>(i);
    }

    BuildRecursive(entities, objectIndices, 0, objectIndices.size());
}



int BvhBuilder::BuildRecursive(const std::vector<GpuEntityMeta>& entities,
    std::vector<int>& indices,
    size_t start,
    size_t end)
{
    size_t count = end - start;
    if (count == 0) return -1;

    // Вычисляем AABB для группы
    glm::vec3 bMin(1e9f);
    glm::vec3 bMax(-1e9f);

    for (size_t i = start; i < end; ++i) {
        glm::vec3 worldMin, worldMax;
        calculateWorldAABB(entities[indices[i]], worldMin, worldMax);
        bMin = glm::min(bMin, worldMin);
        bMax = glm::max(bMax, worldMax);
    }

    // Создаём узел
    int currentNodeIdx = static_cast<int>(flatNodes.size());
    flatNodes.emplace_back();

    flatNodes[currentNodeIdx].boxMin = glm::vec4(bMin, -1.0f);
    flatNodes[currentNodeIdx].boxMax = glm::vec4(bMax, -1.0f);

    // Лист
    if (count == 1) {
        flatNodes[currentNodeIdx].metaData.x = indices[start];
        return currentNodeIdx;
    }

    // Выбираем ось разделения
    glm::vec3 size = bMax - bMin;
    int axis = 0;
    if (size.y > size.x && size.y > size.z) axis = 1;
    if (size.z > size.x && size.z > size.y) axis = 2;

    float splitPos = (bMin[axis] + bMax[axis]) * 0.5f;

    // Разделяем объекты
    auto midIt = std::partition(indices.begin() + start, indices.begin() + end,
        [&](int idx) {
            glm::vec3 worldMin, worldMax;
            calculateWorldAABB(entities[idx], worldMin, worldMax);
            glm::vec3 center = (worldMin + worldMax) * 0.5f;
            return center[axis] < splitPos;
        });

    size_t mid = std::distance(indices.begin(), midIt);

    // Защита от вырождения
    if (mid == start || mid == end) {
        mid = start + count / 2;
    }

    // Внутренний узел
    flatNodes[currentNodeIdx].metaData.x = -1;

    int leftIdx = BuildRecursive(entities, indices, start, mid);
    int rightIdx = BuildRecursive(entities, indices, mid, end);

    flatNodes[currentNodeIdx].boxMin.w = static_cast<float>(leftIdx);
    flatNodes[currentNodeIdx].boxMax.w = static_cast<float>(rightIdx);

    return currentNodeIdx;
}
