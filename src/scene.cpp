#include "scene.hpp"
#include <queue>
#include <fstream>
#include <iostream>
#include <cstring>
#include <algorithm>
#include "VoxelModifier.hpp"


void VoxelScene::Init()
{


    uint32_t macroSSBO = 0, poolSSBO = 0, metaSSBO = 0, chunkSSBO = 0, matSSBO = 0, bvhSSBO = 0;

    // ИСПРАВЛЕНО: Генерируем ВСЕ 5 буферов одной командой, чтобы не забыть ни один
    glGenBuffers(1, &macroSSBO);
    glGenBuffers(1, &poolSSBO);
    glGenBuffers(1, &metaSSBO);
    glGenBuffers(1, &chunkSSBO);
    glGenBuffers(1, &matSSBO);
    glGenBuffers(1, &bvhSSBO);

    // Выделяем память под Макро-Ленту (1 миллион индексов)
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, macroSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 1024 * 1024 * sizeof(uint32_t), nullptr, GL_DYNAMIC_DRAW);

    // Выделяем память под Пул Бриков (100 000 штук по 512 байт)
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, poolSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 1000000 * sizeof(Brick), nullptr, GL_DYNAMIC_DRAW);

    // Выделяем память под Мета-данные объектов (максимум 1000 штук)
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, metaSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 5000 * sizeof(GpuEntityMeta), nullptr, GL_DYNAMIC_DRAW);
    
    // Выделяем память под Мета-данные чанков (максимум 1000 штук)
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, metaSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 5000 * sizeof(GpuChunkMeta), nullptr, GL_DYNAMIC_DRAW);

    // Выделяем память под Палитру Материалов (ровно 256 штук)
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, matSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 1000*256 * sizeof(Material), nullptr, GL_STATIC_DRAW);

    // Выделяем память под BVH-дерево (например, с запасом под 2000 узлов)
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, bvhSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 5000 * sizeof(BVHNode), nullptr, GL_DYNAMIC_DRAW);

    // Отвязываем буфер для безопасности
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);


    manager.SetGpuBuffers(macroSSBO, poolSSBO, metaSSBO, chunkSSBO, matSSBO, bvhSSBO);

}

void VoxelScene::Update(float dt) {
    objectIDs = manager.GetAllObjectIds();

    
    manager.Update(dt);

    // 1. Мета-данные объектов (матрицы, размеры, оффсеты)
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, manager.GetMetaBufferID());

    // 2. Лента макро-сеток
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, manager.GetMacroGridBufferID());

    // 3. Пул вокселей (брики)
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, manager.GetBrickPoolBufferID());
    
    // 4. Лента материалов (Палитры)
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, manager.GetMaterialsBufferID());
    
    // 5. BVH дерево
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, manager.GetBvhBufferID());


    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, manager.GetChunkBufferID());
}

// ==========================================
// ЗАГРУЗКА VOX ФАЙЛОВ
// ==========================================

uint32_t VoxelScene::LoadVox(const std::string& path)
{
    uint32_t id = 0;
    std::ifstream file(path, std::ios::binary);
    if (!file) 
    {
        printf("No file ", path);
        return false;
    }

    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(fileSize);
    file.read((char*)buffer.data(), fileSize);

    const ogt_vox_scene* scene =
        ogt_vox_read_scene(buffer.data(), (uint32_t)buffer.size());

    if (!scene) return id;



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
                    glm::ivec3 currentPos(x, z, y); 
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

        obj.transform.scale = glm::vec3(0.1f);

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

        
        obj.isDirty = true;

        // 3. Заполнение палитры
        for (int p = 0; p < 256; p++)
        {
            ogt_vox_rgba c = scene->palette.color[p];
            Material m;
            m.Color[0] = c.r;
            m.Color[1] = c.g;
            m.Color[2] = c.b;
            obj.materials[p] = m;
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
        
        id = manager.RegisterObject(std::move(obj));
    }

    ogt_vox_destroy_scene(scene);
    return id;
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

void VoxelScene::SplitObject(uint32_t objectIndex)
{
    // 1. Проверяем границы индекса в сцене
    if (objectIndex >= objectIDs.size()) return;
    uint32_t targetId = objectIDs[objectIndex]; // Получаем реальный ID объекта в менеджере

    VoxelObject& srcObj = manager.GetObject(targetId);
    std::vector<VoxelIsland> islands = FindIslands(srcObj);

    // Если объект монолитный (1 остров или пустой), ничего делить не нужно
    if (islands.size() <= 1) {
        return;
    }

    // Делаем полную копию оригинала ДО любых изменений и удалений
    VoxelObject originalObj = srcObj;

    // Создаём контейнер для новых разделенных островов
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

        // Копируем материалы палитры
        std::memcpy(newObj.materials,
            originalObj.materials,
            sizeof(originalObj.materials));

        // Копируем базовый трансформ
        newObj.transform.position = originalObj.transform.position;
        newObj.transform.rotationEuler = originalObj.transform.rotationEuler;
        newObj.transform.qrotation = originalObj.transform.qrotation;
        newObj.transform.scale = originalObj.transform.scale;

        // ИСПРАВЛЕНО: Смещаем позицию острова с учетом его локального поворота
        // Смещение в локальном пространстве объекта
        glm::vec3 localOffset = glm::vec3(minB) * originalObj.transform.scale;
        // Поворачиваем локальное смещение кватернионом объекта и прибавляем к мировой позиции
        newObj.transform.position += originalObj.transform.qrotation * localOffset;

        // Переносим воксели в локальную сетку нового острова
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

    // --- ИСПРАВЛЕНО: МЕНЕДЖМЕНТ ПАМЯТИ И ОБНОВЛЕНИЕ СЦЕНЫ ---

    // 1. Полностью удаляем старый родительский объект с CPU, GPU и освобождаем его брики/ленту
    manager.UnregisterObject(targetId);

    // Удаляем старый ID из списка идентификаторов сцены
    objectIDs.erase(objectIDs.begin() + objectIndex);

    // 2. Регистрируем новые получившиеся острова в менеджере и добавляем их ID в сцену
    for (size_t i = 0; i < newObjects.size(); ++i) {
        // Регистрируем объект в менеджере памяти и получаем его новый уникальный ID
        uint32_t newId = manager.RegisterObject(std::move(newObjects[i]));

        if (newId != 0) {
            // Добавляем новый ID в список объектов сцены
            objectIDs.push_back(newId);
        }
    }

    std::cout << "Split complete. Target removed. New islands added. Total scene objects: " << objectIDs.size() << std::endl;
}
// ==========================================
// СБОРКА ДАННЫХ ДЛЯ GPU
// ==========================================

bool VoxelScene::RemoveVoxelByRay(glm::vec3 rayOrigin, glm::vec3 rayDir)
{
    // 1. Сортируем ID объектов по расстоянию от камеры
    std::vector<std::pair<float, uint32_t>> sortedObjectIds;

    for (uint32_t id : objectIDs) {
        // Просим у менеджера доступ к объекту по его ID
        const VoxelObject& obj = manager.GetObject(id);

        glm::vec3 objCenter = obj.transform.position;
        float dist = glm::length(objCenter - rayOrigin);
        sortedObjectIds.push_back({ dist, id });
    }
    std::sort(sortedObjectIds.begin(), sortedObjectIds.end());

    glm::ivec3 hitVoxelPos = glm::ivec3(0);

    // 2. Проверяем объекты от ближнего к дальнему
    for (auto& [dist, id] : sortedObjectIds) {
        // Получаем изменяемую ссылку на объект из менеджера
        VoxelObject& obj = manager.GetObject(id);

        if (VoxelModifier::RemoveVoxelByRay(obj.voxelMap,obj.GetFinalModelMatrix(), rayOrigin, rayDir, hitVoxelPos)) {
            
            manager.OnObjectVoxelsChanged(id, hitVoxelPos.x, hitVoxelPos.y, hitVoxelPos.z);
            return true;
        }
    }
    return false;
}


