#include "Manager.hpp"

#include <algorithm>

VoxelManager::VoxelManager(uint32_t maxBricks) {
    maxBricksInPool = maxBricks;

    cpuBrickPool.resize(maxBricks);
    brickInUse.resize(maxBricks, false);

    // ИНДЕКС 0 РЕЗЕРВИРУЕМ ПОД ВОЗДУХ/ПУСТОТУ!
    brickInUse[0] = true;

    // Заполняем пул свободными индексами (начиная с 1)
    freeBrickIndices.reserve(maxBricks - 1);
    for (uint32_t i = 1; i < maxBricks; ++i) {
        freeBrickIndices.push_back(i);
    }

    int totalElements = TEXTURE_SIZE * TEXTURE_SIZE * TEXTURE_SIZE;

    chunkSlotInUse.push_back(true);;


    //КАРТА ЧАНКОВ
    glGenTextures(1, &chunkMap);
    glBindTexture(GL_TEXTURE_3D, chunkMap);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    // REPEAT обязателен! Он автоматически зацикливает координаты в шейдере
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);

    glTexImage3D(GL_TEXTURE_3D, 0, GL_R32UI,
        TEXTURE_SIZE, TEXTURE_SIZE, TEXTURE_SIZE,
        0, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);

    //КАРТА BVH
    glGenTextures(1, &bvhMap);
    glBindTexture(GL_TEXTURE_3D, bvhMap);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    // REPEAT обязателен! Он автоматически зацикливает координаты в шейдере
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);

    glTexImage3D(GL_TEXTURE_3D, 0, GL_R32UI,
        TEXTURE_SIZE, TEXTURE_SIZE, TEXTURE_SIZE,
        0, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);

}


void VoxelManager::SetGpuBuffers(uint32_t macroBuffer, uint32_t poolBuffer, uint32_t metaBuffer, uint32_t chunkBuffer, uint32_t materialsBuffer, uint32_t bvhBuffer) {
    gpuMacroGridBufferID = macroBuffer;
    gpuBrickPoolBufferID = poolBuffer;
    gpuMetaBufferID = metaBuffer;
    gpuChunkBufferID = chunkBuffer;
    gpuMaterialsBufferID = materialsBuffer;
    gpuBvhBufferID = bvhBuffer;
}


void VoxelManager::UploadBVH()
{
    if (gpuBvhBufferID == 0 || bvhNodes.empty()) return;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, gpuBvhBufferID);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bvhNodes.size() * sizeof(BVHNode),
        bvhNodes.data(), GL_DYNAMIC_DRAW);
}


void VoxelManager::LoadWorldBasePalette(const std::vector<Material>& baseMaterials)
{
    if (gpuMaterialsBufferID == 0) {
        std::cout << "ERROR: gpuMaterialsBufferID is not initialized yet!" << std::endl;
        return;
    }

    if (baseMaterials.empty()) return;

    // Базовый размер палитры мира (первые 256 слотов)
    const uint32_t BASE_PALETTE_SIZE = 256;
    size_t countToLoad = std::min(baseMaterials.size(), static_cast<size_t>(BASE_PALETTE_SIZE));

    // Привязываем твой стандартный буфер материалов
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, gpuMaterialsBufferID);

    // Записываем строго в НАЧАЛО буфера (смещение 0)
    glBufferSubData(GL_SHADER_STORAGE_BUFFER,
        0,
        countToLoad * sizeof(Material),
        baseMaterials.data());

    // Защищаем менеджер памяти: гарантируем, что объекты начнут выделяться ПОСЛЕ базовой палитры
    if (nextFreePaletteOffset < BASE_PALETTE_SIZE) {
        nextFreePaletteOffset = BASE_PALETTE_SIZE;
    }


}


VoxelObject& VoxelManager::GetObject(uint32_t id)
{
    auto it = activeObjects.find(id);
    if (it == activeObjects.end()) {
        static VoxelObject dummy;
        return dummy;
    }
    return it->second;
}


const VoxelObject& VoxelManager::GetObject(uint32_t id) const
{
    auto it = activeObjects.find(id);
    if (it == activeObjects.end()) {
        static const VoxelObject dummy;
        return dummy;
    }
    return it->second;
}


uint32_t VoxelManager::RegisterObject(VoxelObject&& obj)
{
    uint32_t objId = 0;

    // 1. Быстрое выделение ID объекта
    if (!freeObjectIndices.empty()) {
        objId = freeObjectIndices.back();
        freeObjectIndices.pop_back();
        objectSlotInUse[objId] = true;
    }
    else {
        objId = objectSlotInUse.size();
        objectSlotInUse.push_back(true);
        maxObjectsEverCreated++;
    }

    // 2. Оптимизированное расширение мета-буфера GPU
    if (objId >= currentGpuMetaCapacity) {
        uint32_t oldCapacity = currentGpuMetaCapacity;
        currentGpuMetaCapacity = (objId + 1) * 2;

        uint32_t newMetaBufferID;
        glGenBuffers(1, &newMetaBufferID);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, newMetaBufferID);
        glBufferData(GL_SHADER_STORAGE_BUFFER, currentGpuMetaCapacity * sizeof(GpuEntityMeta), nullptr, GL_DYNAMIC_DRAW);

        if (gpuMetaBufferID != 0 && oldCapacity > 0) {
            glBindBuffer(GL_COPY_READ_BUFFER, gpuMetaBufferID);
            glBindBuffer(GL_COPY_WRITE_BUFFER, newMetaBufferID);
            glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, oldCapacity * sizeof(GpuEntityMeta));
            glDeleteBuffers(1, &gpuMetaBufferID);
        }
        gpuMetaBufferID = newMetaBufferID;
    }

    uint32_t totalCells = obj.GetMacroGridSize();
    uint32_t allocatedOffset = 0;

    if (FindFreeBlock(totalCells, allocatedOffset)) {
        obj.macroGridOffset = allocatedOffset;
    }
    else {
        obj.macroGridOffset = nextFreeLentaOffset;
        nextFreeLentaOffset += totalCells;

        if (nextFreeLentaOffset > cpuMacroGridLenta.size()) {
            cpuMacroGridLenta.resize(nextFreeLentaOffset, 0);
        }
    }
    objectMacroOffsets[objId] = obj.macroGridOffset;

    std::vector<uint32_t> localGrid(totalCells, 0);

    uint32_t currentPaletteOffset = nextFreePaletteOffset;
    obj.matOffset = currentPaletteOffset;
    nextFreePaletteOffset += 256;

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, gpuMaterialsBufferID);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, currentPaletteOffset * sizeof(Material), 256 * sizeof(Material), obj.materials);

    // --- ОПТИМИЗАЦИЯ ПАМЯТИ CPU (СТАТИЧЕСКИЕ БУФЕРЫ) ---
    // thread_local/static исключают аллокации при каждом вызове функции. 
    // Память выделяется один раз под максимальный размер и просто переиспользуется.
    static std::vector<uint32_t> allocatedBricksThisTurn;
    static std::vector<Brick> tempBrickBuffer;
    static std::vector<uint32_t> tempBrickIndices;

    allocatedBricksThisTurn.clear();
    tempBrickBuffer.clear();
    tempBrickIndices.clear();

    uint32_t maxPossibleBricks = obj.bricksX * obj.bricksY * obj.bricksZ;
    allocatedBricksThisTurn.reserve(maxPossibleBricks);
    tempBrickBuffer.reserve(maxPossibleBricks);
    tempBrickIndices.reserve(maxPossibleBricks);

    const auto& mapSize = obj.voxelMap.size;

    // Обход Z -> Y -> X
    for (int bz = 0; bz < obj.bricksZ; ++bz) {
        for (int by = 0; by < obj.bricksY; ++by) {
            for (int bx = 0; bx < obj.bricksX; ++bx) {

                Brick tempBrick = {};
                bool hasGeometry = false;

                for (int vz = 0; vz < 8; ++vz) {
                    int gz = (bz << 3) + vz;
                    if (gz >= mapSize.z) continue;

                    for (int vy = 0; vy < 8; ++vy) {
                        int gy = (by << 3) + vy;
                        if (gy >= mapSize.y) continue;

                        for (int vx = 0; vx < 8; ++vx) {
                            int gx = (bx << 3) + vx;
                            if (gx >= mapSize.x) continue;

                            Voxel v = obj.voxelMap.GetVoxel(gx, gy, gz);
                            if (v.ID != 0) {
                                uint16_t idx = Brick::GetLinearIndex(vx, vy, vz);
                                tempBrick.Data[idx].ID = v.ID;
                                hasGeometry = true;
                            }
                        }
                    }
                }

                if (hasGeometry) {
                    if (freeBrickIndices.empty()) {
                        std::cout << "CRITICAL: Out of Voxel Brick Memory on GPU!" << std::endl;

                        for (uint32_t id : allocatedBricksThisTurn) {
                            brickInUse[id] = false;
                            freeBrickIndices.push_back(id);
                        }
                        MemoryBlock blockToReturn = { obj.macroGridOffset, totalCells };
                        AddFreeBlock(blockToReturn);
                        objectMacroOffsets.erase(objId);
                        objectSlotInUse[objId] = false;
                        freeObjectIndices.push_back(objId);

                        return 0;
                    }

                    uint32_t globalBrickId = freeBrickIndices.back();
                    freeBrickIndices.pop_back();

                    brickInUse[globalBrickId] = true;
                    allocatedBricksThisTurn.push_back(globalBrickId);

                    if (globalBrickId >= cpuBrickPool.size()) {
                        cpuBrickPool.resize(globalBrickId + 1);
                        brickInUse.resize(globalBrickId + 1, false);
                    }

                    cpuBrickPool[globalBrickId] = tempBrick;

                    uint32_t localMacroIdx = obj.GetMacroIndex(bx, by, bz);
                    localGrid[localMacroIdx] = globalBrickId;

                    tempBrickBuffer.push_back(tempBrick);
                    tempBrickIndices.push_back(globalBrickId);
                }
            }
        }
    }

    // --- ФИНАЛЬНЫЙ СКЛЕЕННЫЙ БАТЧИНГ НА GPU ---
    if (!tempBrickBuffer.empty()) {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, gpuBrickPoolBufferID);

        size_t i = 0;
        while (i < tempBrickBuffer.size()) {
            size_t start = i;
            // Склеиваем идущие подряд ID бриков в один непрерывный кусок
            while (i + 1 < tempBrickBuffer.size() && tempBrickIndices[i + 1] == tempBrickIndices[i] + 1) {
                i++;
            }
            size_t count = i - start + 1;

            // Вызывается один раз для всего монолитного куска (вместо 512 раз)
            glBufferSubData(GL_SHADER_STORAGE_BUFFER,
                tempBrickIndices[start] * sizeof(Brick),
                count * sizeof(Brick),
                &tempBrickBuffer[start]);
            i++;
        }
    }

    // 3. Копируем макро-сетку
    std::memcpy(&cpuMacroGridLenta[obj.macroGridOffset], localGrid.data(), totalCells * sizeof(uint32_t));

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, gpuMacroGridBufferID);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, obj.macroGridOffset * sizeof(uint32_t), totalCells * sizeof(uint32_t), localGrid.data());

    obj.isNew = false;
    obj.voxelsChanged = false;

    GpuEntityMeta meta = {};
    meta.sizeInBricks.w = obj.macroGridOffset;
    meta.sizeInVoxels.w = currentPaletteOffset;

    obj.UpdateBoundsAndMatrices(meta);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, gpuMetaBufferID);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, objId * sizeof(GpuEntityMeta), sizeof(GpuEntityMeta), &meta);

    //obj.bodyID = physicsWorld.AddBody(&obj.voxelMap, JPH::EMotionType::Kinematic, obj.transform.position, glm::radians(obj.transform.rotationEuler),obj.transform.offsetCollision);

    activeObjects[objId] = std::move(obj);

    return objId;
}


void VoxelManager::UnregisterChunk(uint32_t objId)
{
    auto it = activeChunks.find(objId);
    if (it == activeChunks.end()) return;

    for (auto itLoading = chunksInGeneration.begin(); itLoading != chunksInGeneration.end(); ++itLoading) {
        if (*itLoading == it->second.pos) {
            chunksInGeneration.erase(itLoading);
            return;
        }
    }

    
    // 1. Возвращаем ID в пул свободных индексов чанков
    freeChunkIndices.push_back(objId);
    chunkSlotInUse[objId] = false;

    Chunk& obj = it->second;
    
    physicsWorld.RemoveBody(obj.bodyID);
    
    uint32_t totalCells = 512; // Фиксированный размер макро-сетки чанка (например, 8х8х8)
    uint32_t offset = obj.macroOffset;

    // Временный статический буфер из нулей, чтобы исключить аллокации памяти на CPU
    static std::vector<uint32_t> zeroGrid(512, 0);

    // 2. Освобождаем все брики, принадлежащие этому чанку
    for (uint32_t i = 0; i < totalCells; ++i) {
        uint32_t globalMacroIdx = offset + i;
        uint32_t globalBrickId = cpuMacroGridLenta[globalMacroIdx];

        // Если ячейка макро-сетки указывает на реальный кирпич в пуле
        if (globalBrickId != 0) {
            brickInUse[globalBrickId] = false;
            freeBrickIndices.push_back(globalBrickId); // Брик снова свободен для новых чанков!
        }
    }

    // 3. Очищаем макро-ленту на CPU нулями
    std::fill_n(cpuMacroGridLenta.begin() + offset, totalCells, 0);

    // 4. Синхронизируем очистку макро-ленты с GPU (затираем старые ID бриков нулями)
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, gpuMacroGridBufferID);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER,
        offset * sizeof(uint32_t),
        totalCells * sizeof(uint32_t),
        zeroGrid.data());

    // 5. Возвращаем освободившийся блок ленты в систему переиспользования памяти
    MemoryBlock freedBlock = { offset, totalCells };
    AddFreeBlock(freedBlock); // Используем твой же аллокатор блоков

    // Удаляем информацию о смещениях чанка из вспомогательных мап
    chunkMacroOffsets.erase(objId);

    // 6. Стираем метаданные чанка на GPU (зануляем vec4, шейдер увидит пустой офсет и пропустит его)
    GpuChunkMeta emptyMeta = {};
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, gpuChunkBufferID);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER,
        objId * sizeof(GpuChunkMeta),
        sizeof(GpuChunkMeta),
        &emptyMeta);

    // 7. Удаляем сам чанк из хэш-карты на CPU
    chunkIDs.erase(obj.pos);
    activeChunks.erase(objId);
}


void VoxelManager::UnregisterObject(uint32_t objId)
{
    auto it = activeObjects.find(objId);
    if (it == activeObjects.end()) return;

    freeObjectIndices.push_back(objId);

    VoxelObject& obj = it->second;
    uint32_t totalCells = obj.GetMacroGridSize();
    uint32_t offset = obj.macroGridOffset;

    //Удаление физики
    physicsWorld.RemoveBody(obj.bodyID);


    // Временный буфер из нулей для полной зачистки макро-сетки на GPU
    std::vector<uint32_t> zeroGrid(totalCells, 0);

    // 1. Освобождаем все брики, принадлежащие этому объекту
    for (uint32_t i = 0; i < totalCells; ++i) {
        uint32_t globalMacroIdx = offset + i;
        uint32_t globalBrickId = cpuMacroGridLenta[globalMacroIdx];

        // Если ячейка указывает на реальный физический брик в пуле
        if (globalBrickId != 0) {
            brickInUse[globalBrickId] = false;
            freeBrickIndices.push_back(globalBrickId); // Индекс снова свободен!
        }
    }

    // 2. Очищаем макро-ленту на CPU нулями
    std::fill_n(cpuMacroGridLenta.begin() + offset, totalCells, 0);

    // 3. Синхронизируем очистку макро-ленты с GPU (затираем старые ID бриков нулями)
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, gpuMacroGridBufferID);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER,
        offset * sizeof(uint32_t),
        totalCells * sizeof(uint32_t),
        zeroGrid.data());

    // 4. Возвращаем освободившийся макро-блок в систему умного переиспользования памяти
    MemoryBlock freedBlock = { offset, totalCells };
    AddFreeBlock(freedBlock);

    // Удаляем информацию о смещениях из вспомогательных мап
    objectMacroOffsets.erase(objId);
    // Если палитры тоже будут переиспользоваться, здесь нужно будет возвращать и space палитры
    objectMaterialOffsets.erase(objId);

    // 5. Стираем метаданные объекта на GPU (шейдер будет игнорировать пустую ноду)
    GpuEntityMeta emptyMeta = {};
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, gpuMetaBufferID);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER,
        objId * sizeof(GpuEntityMeta),
        sizeof(GpuEntityMeta),
        &emptyMeta);

    // 6. Удаляем сам объект из хэш-карты на CPU
    activeObjects.erase(objId);

}


std::vector<uint32_t> VoxelManager::GetAllObjectIds() const
{
    std::vector<uint32_t> ids;
    ids.reserve(activeObjects.size());
    for (const auto& [id, obj] : activeObjects) {
        ids.push_back(id);
    }
    return ids;
}


void VoxelManager::OnObjectVoxelsChanged(uint32_t objId, int voxelX, int voxelY, int voxelZ) {
    auto it = activeObjects.find(objId);
    if (it == activeObjects.end()) return;
    VoxelObject& obj = it->second;

    int bx = voxelX >> 3;
    int by = voxelY >> 3;
    int bz = voxelZ >> 3;

    uint32_t localMacroIdx = obj.GetMacroIndex(bx, by, bz);
    uint32_t globalMacroLentaIdx = obj.macroGridOffset + localMacroIdx;
    uint32_t globalBrickId = cpuMacroGridLenta[globalMacroLentaIdx];

    Brick updatedBrick = {};
    bool hasGeometry = false;

    for (int vy = 0; vy < 8; ++vy) {
        for (int vz = 0; vz < 8; ++vz) {
            for (int vx = 0; vx < 8; ++vx) {
                int gx = (bx << 3) + vx;
                int gy = (by << 3) + vy;
                int gz = (bz << 3) + vz;

                if (gx >= obj.voxelMap.size.x || gy >= obj.voxelMap.size.y || gz >= obj.voxelMap.size.z)
                    continue;

                Voxel v = obj.voxelMap.GetVoxel(gx, gy, gz);
                if (v.ID != 0) {
                    uint16_t idx = Brick::GetLinearIndex(vx, vy, vz);
                    updatedBrick.Data[idx].ID = v.ID;
                    hasGeometry = true;
                }
            }
        }
    }

    if (hasGeometry) {
        if (globalBrickId == 0) {
            if (freeBrickIndices.empty()) return;
            globalBrickId = freeBrickIndices.front();
            freeBrickIndices.erase(freeBrickIndices.begin());
            brickInUse[globalBrickId] = true;

            cpuMacroGridLenta[globalMacroLentaIdx] = globalBrickId;
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, gpuMacroGridBufferID);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, globalMacroLentaIdx * sizeof(uint32_t),
                sizeof(uint32_t), &globalBrickId);
        }

        if (globalBrickId >= cpuBrickPool.size()) {
            cpuBrickPool.resize(globalBrickId + 1);
            brickInUse.resize(globalBrickId + 1, false);
        }

        cpuBrickPool[globalBrickId] = updatedBrick;
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, gpuBrickPoolBufferID);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, globalBrickId * sizeof(Brick),
            sizeof(Brick), &updatedBrick);
    }
    else if (globalBrickId != 0) {
        uint32_t zero = 0;
        cpuMacroGridLenta[globalMacroLentaIdx] = zero;
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, gpuMacroGridBufferID);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, globalMacroLentaIdx * sizeof(uint32_t),
            sizeof(uint32_t), &zero);

        cpuBrickPool[globalBrickId] = Brick{};
        brickInUse[globalBrickId] = false;
        freeBrickIndices.push_back(globalBrickId);
        std::sort(freeBrickIndices.begin(), freeBrickIndices.end());
    }
}


void VoxelManager::Update(float dt) {
    physicsWorld.Update(dt,this);

    for (auto& [id, obj] : activeObjects) {
        obj.Update(dt);

        if (obj.isDirty) {
            GpuEntityMeta meta = {};
            meta.sizeInBricks.w = obj.macroGridOffset;
            meta.sizeInVoxels.w = obj.matOffset;

            obj.UpdateBoundsAndMatrices(meta);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, gpuMetaBufferID);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, id * sizeof(GpuEntityMeta),
                sizeof(GpuEntityMeta), &meta);
        }
    }

    

    UpdateBVH();
    


    UpdatePendingChunksGpu();

    UploadBVH();
    
    
    
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_3D, chunkMap);

    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_3D, bvhMap);

}


void VoxelManager::InitiateChunkRegistration(std::unique_ptr<Chunk> obj)

{
    // 1. Быстрая выдача ID чанка
    uint32_t objId = 0;
    if (!freeChunkIndices.empty()) {
        objId = freeChunkIndices.back();
        freeChunkIndices.pop_back();
        chunkSlotInUse[objId] = true;
    }
    else {
        objId = chunkSlotInUse.size();
        chunkSlotInUse.push_back(true);
        maxChunksEverCreated++;
    }

    // 2. Быстрый расчет макро-офсета ленты (через стрелочку)
    uint32_t totalCells = 512;
    uint32_t allocatedOffset = 0;
    if (FindFreeBlock(totalCells, allocatedOffset)) {
        obj->macroOffset = allocatedOffset;
    }
    else {
        obj->macroOffset = nextFreeLentaOffset;
        nextFreeLentaOffset += totalCells;
        if (nextFreeLentaOffset > cpuMacroGridLenta.size()) {
            cpuMacroGridLenta.resize(nextFreeLentaOffset, 0);
        }
    }
    chunkMacroOffsets[objId] = obj->macroOffset;

    // 3. Выделяем память под результат
    auto resultShared = std::make_shared<ChunkProcessingResult>();

    chunksInGeneration.push_back(obj->pos);

    // Просто перекладываем указатель в результат. Никаких make_unique!
    resultShared->chunkObjPtr = std::move(obj);

    uint32_t currentMacroOffset = resultShared->chunkObjPtr->macroOffset;

    // 4. Запускаем асинхронный поток
    auto future = std::async(std::launch::async, [this, objId, macroOffset = currentMacroOffset, resultShared]() mutable {
        // Передаем в функцию чистую ссылку на объект внутри указателя (*resultShared->chunkObjPtr)
        ProcessChunkData(objId, *(resultShared->chunkObjPtr), macroOffset, this->freeBrickIndices, *resultShared);
        });

    // 5. Запоминаем таску
    pendingChunks.push_back({ std::move(future), resultShared });
}


void VoxelManager::ProcessChunkData(uint32_t objId, Chunk& obj, uint32_t macroOffset, std::vector<uint32_t>& freeBrickIndicesPool, ChunkProcessingResult& result)
{

    result.chunkId = objId;
    result.macroOffset = macroOffset;

    uint32_t totalCells = 512;
    result.localGrid.resize(totalCells, 0);

    uint32_t maxPossibleBricks = obj.bricksX * obj.bricksY * obj.bricksZ;
    result.tempBrickBuffer.reserve(maxPossibleBricks);
    result.tempBrickIndices.reserve(maxPossibleBricks);
    result.allocatedBricks.reserve(maxPossibleBricks);

    const auto& mapSize = obj.voxelMap.size;

    // --- ВАШ ТЯЖЕЛЫЙ ЦИКЛ ОБХОДА (Z -> Y -> X) ---
    for (int bz = 0; bz < obj.bricksZ; ++bz) {
        for (int by = 0; by < obj.bricksY; ++by) {
            for (int bx = 0; bx < obj.bricksX; ++bx) {
                Brick tempBrick = {};
                bool hasGeometry = false;

                for (int vz = 0; vz < 8; ++vz) {
                    int gz = (bz << 3) + vz; if (gz >= mapSize.z) continue;
                    for (int vy = 0; vy < 8; ++vy) {
                        int gy = (by << 3) + vy; if (gy >= mapSize.y) continue;
                        for (int vx = 0; vx < 8; ++vx) {
                            int gx = (bx << 3) + vx; if (gx >= mapSize.x) continue;

                            Voxel v = obj.voxelMap.GetVoxel(gx, gy, gz);
                            if (v.ID != 0) {
                                uint16_t idx = Brick::GetLinearIndex(vx, vy, vz);
                                tempBrick.Data[idx].ID = v.ID;
                                hasGeometry = true;
                            }
                        }
                    }
                }

                if (hasGeometry) {
                    std::lock_guard<std::mutex> lock(freeBrickIndicesMutex); // Залочили пул
                    if (freeBrickIndicesPool.empty()) {
                        result.success = false;
                        return;
                    }
                    uint32_t globalBrickId = freeBrickIndicesPool.back();
                    freeBrickIndicesPool.pop_back();
                    // Мьютекс сам откроется здесь при выходе из блока if (hasGeometry)

                    result.allocatedBricks.push_back(globalBrickId);
                    result.localGrid[obj.GetMacroIndex(bx, by, bz)] = globalBrickId;
                    result.tempBrickBuffer.push_back(tempBrick);
                    result.tempBrickIndices.push_back(globalBrickId);
                }

            }
        }
    }

    return;
}


void VoxelManager::UpdatePendingChunksGpu()
{
    // Бежим по всей очереди ожидающих чанков
    for (auto it = pendingChunks.begin(); it != pendingChunks.end(); ) {

        // Проверяем статус потока БЕЗ блокировки (wait_for со значением 0 не ждет, а мгновенно возвращает статус)
        if (it->first.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {

            // 1. Поток завершился! Достаем shared_ptr с результатами
            auto res = it->second;

            // 2. Если в потоке произошла критическая ошибка (кончились ID бриков на GPU)
            if (!res->success) {
                std::cout << "CRITICAL: Out of Voxel Brick Memory on GPU!" << std::endl;

                // Возвращаем все ресурсы назад под мьютексом
                {
                    std::lock_guard<std::mutex> lock(freeBrickIndicesMutex);
                    for (uint32_t id : res->allocatedBricks) {
                        brickInUse[id] = false;
                        freeBrickIndices.push_back(id);
                    }
                }

                MemoryBlock blockToReturn = { res->macroOffset, 512 };
                AddFreeBlock(blockToReturn);
                chunkMacroOffsets.erase(res->chunkId);
                chunkSlotInUse[res->chunkId] = false;
                freeChunkIndices.push_back(res->chunkId);

                // Удаляем таску и идем дальше
                for (auto itInGen = chunksInGeneration.begin(); itInGen != chunksInGeneration.end(); ++itInGen) {
                    if (*itInGen == res->chunkObjPtr->pos) {
                        chunksInGeneration.erase(itInGen);
                        break;
                    }
                }

                it = pendingChunks.erase(it);
                continue;
            }

            // 3. Синхронизируем CPU пул бриков (записываем то, что поток собрал)
            for (size_t k = 0; k < res->allocatedBricks.size(); ++k) {
                uint32_t globalBrickId = res->allocatedBricks[k];
                brickInUse[globalBrickId] = true;
                if (globalBrickId >= cpuBrickPool.size()) {
                    cpuBrickPool.resize(globalBrickId + 1);
                    brickInUse.resize(globalBrickId + 1, false);
                }
                cpuBrickPool[globalBrickId] = res->tempBrickBuffer[k];
            }

            // 4. --- ЗАЛИВКА НА GPU (ОДНИМ МАХОМ) ---

            // Проверяем и расширяем мета-буфер GPU, если чанк вышел за границы
            if (res->chunkId >= currentGpuChunksCapacity) {
                uint32_t oldCapacity = currentGpuChunksCapacity;
                currentGpuChunksCapacity = (res->chunkId + 1) * 2;
                uint32_t newChunkBufferID;
                glGenBuffers(1, &newChunkBufferID);
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, newChunkBufferID);
                glBufferData(GL_SHADER_STORAGE_BUFFER, currentGpuChunksCapacity * sizeof(GpuChunkMeta), nullptr, GL_DYNAMIC_DRAW);

                if (gpuChunkBufferID != 0 && oldCapacity > 0) {
                    glBindBuffer(GL_COPY_READ_BUFFER, gpuChunkBufferID);
                    glBindBuffer(GL_COPY_WRITE_BUFFER, newChunkBufferID);
                    glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, oldCapacity * sizeof(GpuChunkMeta));
                    glDeleteBuffers(1, &gpuChunkBufferID);
                }
                gpuChunkBufferID = newChunkBufferID;
            }

            // Оптимизированный батчинг бриков в SSBO
            if (!res->tempBrickBuffer.empty()) {
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, gpuBrickPoolBufferID);
                size_t i = 0;
                while (i < res->tempBrickBuffer.size()) {
                    size_t start = i;
                    while (i + 1 < res->tempBrickBuffer.size() && res->tempBrickIndices[i + 1] == res->tempBrickIndices[i] + 1) {
                        i++;
                    }
                    size_t count = i - start + 1;
                    glBufferSubData(GL_SHADER_STORAGE_BUFFER, res->tempBrickIndices[start] * sizeof(Brick), count * sizeof(Brick), &res->tempBrickBuffer[start]);
                    i++;
                }
            }

            // Копируем макро-сетку чанка в общую CPU ленту
            uint32_t totalCells = 512;
            std::memcpy(&cpuMacroGridLenta[res->macroOffset], res->localGrid.data(), totalCells * sizeof(uint32_t));

            // Заливаем макро-сетку на GPU
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, gpuMacroGridBufferID);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, res->macroOffset * sizeof(uint32_t), totalCells * sizeof(uint32_t), res->localGrid.data());

            // Заливаем метаданные самого чанка (позиция)
            GpuChunkMeta meta = {};

            glm::ivec3 pos = res->chunkObjPtr->pos;

            meta.pos.x = pos.x;
            meta.pos.y = pos.y;
            meta.pos.z = pos.z;
            meta.pos.w = res->macroOffset;

            res->chunkObjPtr->bodyID = physicsWorld.AddBody(&res->chunkObjPtr->voxelMap, JPH::EMotionType::Static, glm::vec3(res->chunkObjPtr->pos) * CHUNK_SIZE_METERS, glm::vec3(0), glm::vec3(3.2));

            glBindBuffer(GL_SHADER_STORAGE_BUFFER, gpuChunkBufferID);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, res->chunkId * sizeof(GpuChunkMeta), sizeof(GpuChunkMeta), &meta);

            // Финальная регистрация в мапах менеджера

            chunkIDs[pos] = res->chunkId;


            SetDataInChunkTex(pos.x, pos.y, pos.z, res->chunkId, chunkMap);


            activeChunks[res->chunkId] = std::move(*res->chunkObjPtr);

            // Удаляем эту готовую таску из вектора ожидания
            it = pendingChunks.erase(it);
        }
        else {
            // Поток еще работает, просто идем к следующему
            ++it;
        }
    }
}


void VoxelManager::SetDataInChunkTex(int globalX, int globalY, int globalZ, uint32_t data, GLuint tex)
{
    int texX = fmod_cyclical(globalX, TEXTURE_SIZE);
    int texY = fmod_cyclical(globalY, TEXTURE_SIZE);
    int texZ = fmod_cyclical(globalZ, TEXTURE_SIZE);

    // 2. Мгновенно отправляем ОДИН 32-битный ID на GPU
    glBindTexture(GL_TEXTURE_3D, tex);

    // ВАЖНО: GL_RED_INTEGER и GL_UNSIGNED_INT говорят OpenGL, 
    // что мы передаем чистый 4-байтовый uint32_t (id) без конвертации в цвета!
    glTexSubImage3D(GL_TEXTURE_3D, 0,
        texX, texY, texZ,   // Координаты ячейки в текстуре
        1, 1, 1,            // Обновляем ровно 1х1х1 пиксель (1 чанк)
        GL_RED_INTEGER,     // Формат данных (Целое число)
        GL_UNSIGNED_INT,    // Тип данных (uint32_t)
        &data                 // Указатель на наш ID чанка
    );

    glBindTexture(GL_TEXTURE_3D, 0);

}


int VoxelManager::getTextureCoord(int globalCoord)
{
    int r = globalCoord % TEXTURE_SIZE;
    return r < 0 ? r + TEXTURE_SIZE : r; // Обработка отрицательных координат
}


void VoxelManager::UpdateBVH()
{

    if (activeObjects.empty()) {
        // Если объектов нет - очищаем всю карту BVH
        ClearAllBVH();
        return;
    }

    // 1. ОЧИЩАЕМ ВСЮ КАРТУ BVH перед обновлением
    // Это решает проблему со старыми данными
    ClearAllBVH();
    bvhNodes.clear();
    bvhNodes.emplace_back(); // dummy node at index 0

    std::unordered_map<glm::ivec3, std::vector<uint32_t>> chunks;

    for (auto& [id, obj] : activeObjects) 
    {
         glm::vec3 min = obj.min;
        glm::vec3 max = obj.max;

        glm::ivec3 cmin;
        glm::ivec3 cmax;

        cmin.x = static_cast<int>(std::floor(min.x / CHUNK_SIZE_METERS));
        cmin.y = static_cast<int>(std::floor(min.y / CHUNK_SIZE_METERS));
        cmin.z = static_cast<int>(std::floor(min.z / CHUNK_SIZE_METERS));

        cmax.x = static_cast<int>(std::ceil(max.x / CHUNK_SIZE_METERS)) - 1;
        cmax.y = static_cast<int>(std::ceil(max.y / CHUNK_SIZE_METERS)) - 1;
        cmax.z = static_cast<int>(std::ceil(max.z / CHUNK_SIZE_METERS)) - 1;

        for (int x = cmin.x; x <= cmax.x; ++x) {
            for (int y = cmin.y; y <= cmax.y; ++y) {
                for (int z = cmin.z; z <= cmax.z; ++z) {
                    chunks[glm::ivec3(x, y, z)].push_back(id);
                }
            }
        }
    }
    // Строим BVH для каждого чанка
    for (auto& [pos, ids] : chunks)
    {
        uint32_t root = BuildChunkBVH(ids, pos);

        SetDataInChunkTex(pos.x,pos.y,pos.z, root,bvhMap);
    }
}

int VoxelManager::BuildChunkBVH(const std::vector<uint32_t>& objects, glm::ivec3 chunkPos)
{
    if (objects.empty()) return -1;

    std::vector<size_t> indices(objects.size());
    for (size_t i = 0; i < objects.size(); ++i) indices[i] = i;

    uint32_t rootIndex = BuildRecursive(objects, indices, 0, indices.size());

    return static_cast<int>(rootIndex);
}

uint32_t VoxelManager::BuildRecursive(const std::vector<uint32_t>& objects,
    std::vector<size_t>& indices,
    size_t from,
    size_t to)
{
    BVHNode node;

    node.boxMin = glm::vec4(FLT_MAX, FLT_MAX, FLT_MAX, -1.0f);
    node.boxMax = glm::vec4(-FLT_MAX, -FLT_MAX, -FLT_MAX, -1.0f);
    node.metaData = glm::ivec4(-1);

    // Вычисляем AABB для всех объектов в диапазоне
    for (size_t i = from; i < to; ++i) {
        uint32_t objIdx = objects[indices[i]];
        auto it = activeObjects.find(objIdx);
        if (it == activeObjects.end()) continue;

        const glm::vec3& objMin = it->second.min;
        const glm::vec3& objMax = it->second.max;

        node.boxMin.x = std::min(node.boxMin.x, objMin.x);
        node.boxMin.y = std::min(node.boxMin.y, objMin.y);
        node.boxMin.z = std::min(node.boxMin.z, objMin.z);
        node.boxMax.x = std::max(node.boxMax.x, objMax.x);
        node.boxMax.y = std::max(node.boxMax.y, objMax.y);
        node.boxMax.z = std::max(node.boxMax.z, objMax.z);
    }

    // Лист: если меньше или равно 4 объектов
    if (to - from == 1) {
        // В листе храним индекс первого объекта
        node.metaData.x = static_cast<int>(objects[indices[from]]);
        node.boxMin.w = -1.0f;
        node.boxMax.w = -1.0f;
        bvhNodes.push_back(node);
        return static_cast<uint32_t>(bvhNodes.size() - 1);
    }

    // Выбираем ось с наибольшим разбросом
    glm::vec3 extent = glm::vec3(node.boxMax) - glm::vec3(node.boxMin);
    int axis = 0;
    if (extent.y > extent.x) axis = 1;
    if (extent.z > extent[axis]) axis = 2;

    // Сортируем по центру вдоль выбранной оси
    std::sort(indices.begin() + from, indices.begin() + to,
        [&](size_t a, size_t b) {
            auto itA = activeObjects.find(objects[a]);
            auto itB = activeObjects.find(objects[b]);

            float centerA = (itA->second.min[axis] + itA->second.max[axis]) * 0.5f;
            float centerB = (itB->second.min[axis] + itB->second.max[axis]) * 0.5f;
            return centerA < centerB;
        }
    );

    // Разбиваем пополам
    size_t mid = from + (to - from) / 2;

    // Рекурсивно строим потомков
    uint32_t leftChild = BuildRecursive(objects, indices, from, mid);
    uint32_t rightChild = BuildRecursive(objects, indices, mid, to);

    // Записываем индексы потомков
    node.boxMin.w = static_cast<float>(leftChild);
    node.boxMax.w = static_cast<float>(rightChild);
    node.metaData.x = -1; // не лист

    bvhNodes.push_back(node);
    return static_cast<uint32_t>(bvhNodes.size() - 1);
}

void VoxelManager::ClearAllBVH() {
    glBindTexture(GL_TEXTURE_3D, bvhMap);

    // Создаем временный буфер с нулями
    const uint32_t TEXTURE_SIZE = 64; // или твой размер
    std::vector<uint32_t> zeroData(TEXTURE_SIZE * TEXTURE_SIZE * TEXTURE_SIZE, 0);

    // Заполняем всю текстуру нулями
    glTexSubImage3D(GL_TEXTURE_3D, 0,
        0, 0, 0,
        TEXTURE_SIZE, TEXTURE_SIZE, TEXTURE_SIZE,
        GL_RED_INTEGER, GL_UNSIGNED_INT, zeroData.data());

    glBindTexture(GL_TEXTURE_3D, 0);
}

void VoxelManager::ClearAll()
{
    // ============================================================
    // 1. УДАЛИТЬ ВСЕ ОБЪЕКТЫ
    // ============================================================
    std::vector<uint32_t> objectIds = GetAllObjectIds();

    for (uint32_t id : objectIds)
    {
        UnregisterObject(id);
    }

    activeObjects.clear();
    objectSlotInUse.clear();
    objectMacroOffsets.clear();
    objectMaterialOffsets.clear();
    freeObjectIndices.clear();

    // ============================================================
    // 2. УДАЛИТЬ ВСЕ ЧАНКИ
    // ============================================================
    std::vector<glm::ivec3> chunkPositions;
    chunkPositions.reserve(chunkIDs.size());

    for (const auto& pair : chunkIDs)
    {
        chunkPositions.push_back(pair.first);
    }

    for (const auto& pos : chunkPositions)
    {
        uint32_t chunkId = chunkIDs[pos];
        UnregisterChunk(chunkId);
    }

    chunkIDs.clear();
    activeChunks.clear();
    chunkSlotInUse.clear();
    chunkMacroOffsets.clear();
    chunksInGeneration.clear();
    freeChunkIndices.clear();

    // ============================================================
    // 3. ОЧИСТИТЬ BVH
    // ============================================================
    bvhNodes.clear();
    chunkBVHIndices.clear();
    ClearAllBVH();

    // ============================================================
    // 4. ОЧИСТИТЬ БРИКИ (BRICK POOL)
    // ============================================================
    cpuBrickPool.clear();
    brickInUse.clear();
    freeBrickIndices.clear();

    // Возвращаем индексы в начальное состояние
    brickInUse.resize(maxBricksInPool, false);
    brickInUse[0] = true; // Индекс 0 резервируем под воздух

    freeBrickIndices.reserve(maxBricksInPool - 1);
    for (uint32_t i = 1; i < maxBricksInPool; ++i)
    {
        freeBrickIndices.push_back(i);
    }

    // ============================================================
    // 5. ОЧИСТИТЬ МАКРО-ЛЕНТУ
    // ============================================================
    cpuMacroGridLenta.clear();
    cpuMacroGridLenta.resize(1024 * 1024, 0); // Или другой размер

    // ============================================================
    // 6. ОЧИСТИТЬ БЛОКИ ПАМЯТИ
    // ============================================================
    freeMacroBlocks.clear();
    freeMacroBlocksBySize.clear();

    // ============================================================
    // 7. СБРОСИТЬ СЧЕТЧИКИ
    // ============================================================
    nextFreeLentaOffset = 1;
    nextFreePaletteOffset = 256;
    currentGpuMetaCapacity = 0;
    currentGpuChunksCapacity = 0;
    maxObjectsEverCreated = 0;
    maxChunksEverCreated = 0;

    // ============================================================
    // 8. СИНХРОНИЗИРОВАТЬ С GPU
    // ============================================================
    SyncGpuAfterClear();
}

void VoxelManager::SyncGpuAfterClear()
{
    // ============================================================
    // 1. ОЧИСТИТЬ МАКРО-ГРИД БУФЕР
    // ============================================================
    if (gpuMacroGridBufferID != 0)
    {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, gpuMacroGridBufferID);

        // Создаем буфер с нулями
        std::vector<uint32_t> zeroData(1024 * 1024, 0);
        glBufferData(
            GL_SHADER_STORAGE_BUFFER,
            zeroData.size() * sizeof(uint32_t),
            zeroData.data(),
            GL_DYNAMIC_DRAW
        );

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    // ============================================================
    // 2. ОЧИСТИТЬ ПУЛ БРИКОВ
    // ============================================================
    if (gpuBrickPoolBufferID != 0)
    {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, gpuBrickPoolBufferID);

        Brick emptyBrick{};
        std::vector<Brick> emptyBricks(100000, emptyBrick);
        glBufferData(
            GL_SHADER_STORAGE_BUFFER,
            emptyBricks.size() * sizeof(Brick),
            emptyBricks.data(),
            GL_DYNAMIC_DRAW
        );

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    // ============================================================
    // 3. ОЧИСТИТЬ МЕТА-БУФЕР
    // ============================================================
    if (gpuMetaBufferID != 0)
    {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, gpuMetaBufferID);

        GpuEntityMeta emptyMeta{};
        std::vector<GpuEntityMeta> emptyMetas(5000, emptyMeta);
        glBufferData(
            GL_SHADER_STORAGE_BUFFER,
            emptyMetas.size() * sizeof(GpuEntityMeta),
            emptyMetas.data(),
            GL_DYNAMIC_DRAW
        );

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    // ============================================================
    // 4. ОЧИСТИТЬ БУФЕР ЧАНКОВ
    // ============================================================
    if (gpuChunkBufferID != 0)
    {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, gpuChunkBufferID);

        GpuChunkMeta emptyChunkMeta{};
        std::vector<GpuChunkMeta> emptyChunkMetas(5000, emptyChunkMeta);
        glBufferData(
            GL_SHADER_STORAGE_BUFFER,
            emptyChunkMetas.size() * sizeof(GpuChunkMeta),
            emptyChunkMetas.data(),
            GL_DYNAMIC_DRAW
        );

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    // ============================================================
    // 5. ОЧИСТИТЬ BVH БУФЕР
    // ============================================================
    if (gpuBvhBufferID != 0)
    {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, gpuBvhBufferID);

        BVHNode emptyNode{};
        std::vector<BVHNode> emptyNodes(5000, emptyNode);
        glBufferData(
            GL_SHADER_STORAGE_BUFFER,
            emptyNodes.size() * sizeof(BVHNode),
            emptyNodes.data(),
            GL_DYNAMIC_DRAW
        );

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    // ============================================================
    // 6. ОЧИСТИТЬ ТЕКСТУРУ ЧАНКОВ
    // ============================================================
    if (chunkMap != 0)
    {
        glBindTexture(GL_TEXTURE_3D, chunkMap);

        std::vector<uint32_t> zeroData(TEXTURE_SIZE * TEXTURE_SIZE * TEXTURE_SIZE, 0);
        glTexSubImage3D(
            GL_TEXTURE_3D,
            0,
            0, 0, 0,
            TEXTURE_SIZE, TEXTURE_SIZE, TEXTURE_SIZE,
            GL_RED_INTEGER,
            GL_UNSIGNED_INT,
            zeroData.data()
        );

        glBindTexture(GL_TEXTURE_3D, 0);
    }

    // ============================================================
    // 7. ОЧИСТИТЬ BVH ТЕКСТУРУ
    // ============================================================
    if (bvhMap != 0)
    {
        glBindTexture(GL_TEXTURE_3D, bvhMap);

        std::vector<uint32_t> zeroData(TEXTURE_SIZE * TEXTURE_SIZE * TEXTURE_SIZE, 0);
        glTexSubImage3D(
            GL_TEXTURE_3D,
            0,
            0, 0, 0,
            TEXTURE_SIZE, TEXTURE_SIZE, TEXTURE_SIZE,
            GL_RED_INTEGER,
            GL_UNSIGNED_INT,
            zeroData.data()
        );

        glBindTexture(GL_TEXTURE_3D, 0);
    }

    // ============================================================
    // 8. БАРЬЕР ПАМЯТИ (ОПЦИОНАЛЬНО)
    // ============================================================
    glMemoryBarrier(GL_ALL_BARRIER_BITS);
}