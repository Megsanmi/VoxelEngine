#pragma once
#include "../VoxelObject/object.hpp"
#include "PhysicsWorld.hpp"
#include <iostream>
#include <set>

#include <glm/glm.hpp>
#ifndef GLM_FORCE_CXX11
#define GLM_FORCE_CXX11
#endif
#include <glm/gtx/hash.hpp>
#include <queue>
#include <mutex>
#include <future>
#include <memory>
#include <GLFW/glfw3.h>


struct MemoryBlock {
    uint32_t offset;
    uint32_t size;

    bool operator<(const MemoryBlock& motorized) const {
        return offset < motorized.offset;
    }
};

struct ClippedObj {
    glm::vec3 min;
    glm::vec3 max;

    uint32_t id;
};

struct CompareBlockBySize {
    bool operator()(const MemoryBlock& a, const MemoryBlock& b) const {
        if (a.size != b.size) return a.size < b.size;
        return a.offset < b.offset;
    }
};
struct ChunkCompileRequest
{
    Chunk* chunk;

    std::vector<uint32_t> brickIds;
    uint32_t chunkId;

    ChunkCompileRequest() = default;

    ChunkCompileRequest(const ChunkCompileRequest&) = delete;
    ChunkCompileRequest& operator=(const ChunkCompileRequest&) = delete;

    ChunkCompileRequest(ChunkCompileRequest&&) noexcept = default;
    ChunkCompileRequest& operator=(ChunkCompileRequest&&) noexcept = default;
};

struct ChunkProcessingResult {
    uint32_t chunkId;
    std::unique_ptr<Chunk> chunkObjPtr; 
    uint32_t macroOffset;
    std::vector<uint32_t> localGrid;
    std::vector<Brick> tempBrickBuffer;
    std::vector<uint32_t> tempBrickIndices;
    std::vector<uint32_t> allocatedBricks;
    bool success = true;
};

class VoxelManager {
public:

    // --- ИДЕНТИФИКАТОРЫ БУФЕРОВ НА GPU ---
    uint32_t gpuMacroGridBufferID = 0;
    uint32_t gpuBrickPoolBufferID = 0;
    uint32_t gpuMetaBufferID = 0;
    uint32_t gpuChunkBufferID = 0;
    uint32_t gpuMaterialsBufferID = 0;
    uint32_t gpuBvhBufferID = 0;

    // --- ЗЕРКАЛЬНЫЕ БУФЕРЫ НА CPU ---
    std::vector<Brick> cpuBrickPool;
    std::vector<uint32_t> cpuMacroGridLenta;
    std::vector<uint32_t> freeObjectIndices;
    std::vector<uint32_t> freeChunkIndices;

    // --- БРИКИ  ---
    std::vector<bool> brickInUse; 
    std::vector<uint32_t> freeBrickIndices;
    std::set<MemoryBlock> freeMacroBlocks;
    std::multiset<MemoryBlock, CompareBlockBySize> freeMacroBlocksBySize; // По размеру для быстрого поиска

    // --- ХРАНИЛИЩЕ ОБЪЕКТОВ ---
    std::unordered_map<uint32_t, VoxelObject> activeObjects;
    std::vector<bool> objectSlotInUse;
    uint32_t currentGpuMetaCapacity = 0;
    uint32_t maxObjectsEverCreated = 0;
    
    std::unordered_map<uint32_t, uint32_t> objectMacroOffsets; // ID -> offset в macro-ленте
    std::unordered_map<uint32_t, uint32_t> objectMaterialOffsets; // ID -> offset в material-ленте
    
    // --- Статические чанки ---
    std::unordered_map<uint32_t, Chunk> activeChunks;
    std::vector<glm::ivec3> chunksInGeneration; // Храним сетку координат, которые сейчас в фоне
    std::unordered_map<glm::ivec3, uint32_t> chunkIDs;

    const float CHUNK_SIZE_METERS = 6.4;
    std::unordered_map<uint32_t, uint32_t> chunkMacroOffsets;
    std::vector<bool> chunkSlotInUse;
    uint32_t currentGpuChunksCapacity = 0;
    uint32_t maxChunksEverCreated = 0;
    
    const int TEXTURE_SIZE = 64;
    
    GLuint chunkMap;
    std::vector<uint8_t> cpuGrid;
    int playerChunkX = 0;
    int playerChunkY = 0;
    int playerChunkZ = 0;

    uint32_t nextFreeLentaOffset = 1;  
    uint32_t nextFreePaletteOffset = 256;

    uint32_t maxBricksInPool;

    //BVH
    std::vector<BVHNode> bvhNodes;
    std::unordered_map<glm::ivec3,uint32_t> chunkBVHIndices;

    GLuint bvhMap;



    //ОЧЕРЕДИ
    std::vector<std::pair<std::future<void>, std::shared_ptr<ChunkProcessingResult>>> pendingChunks;
    std::mutex freeBrickIndicesMutex;


    PhysicsSystem physicsWorld;

    bool FindFreeBlock(uint32_t neededSize, uint32_t& outOffset) {
        // Ищем блок с минимальным размером >= neededSize. 
        // offset ставим 0, так как это минимально возможный offset.
        auto it = freeMacroBlocksBySize.lower_bound({ 0, neededSize });

        if (it != freeMacroBlocksBySize.end()) {
            MemoryBlock block = *it;
            outOffset = block.offset;

            // Удаляем строго по итератору из multiset (это быстрее и безопаснее)
            freeMacroBlocksBySize.erase(it);

            // Удаляем из обычного сета по значению (там поиск по offset, сработает отлично)
            freeMacroBlocks.erase(block);

            // Если блок больше нужного, возвращаем остаток в систему
            if (block.size > neededSize) {
                MemoryBlock remaining;
                remaining.offset = block.offset + neededSize;
                remaining.size = block.size - neededSize;
                AddFreeBlock(remaining); // Метод сам добавит в оба контейнера
            }
            return true;
        }
        return false;
    }

    void AddFreeBlock(const MemoryBlock& newBlock) {
        if (newBlock.size == 0) return;

        uint32_t mergedOffset = newBlock.offset;
        uint32_t mergedSize = newBlock.size;

        // Ищем позицию, куда должен встать новый блок по смещению
        auto itNext = freeMacroBlocks.lower_bound({ mergedOffset, 0 });

        // 1. Проверяем левого соседа (блок перед новым)
        if (itNext != freeMacroBlocks.begin()) {
            auto itPrev = std::prev(itNext); // Безопасно шаг назад
            if (itPrev->offset + itPrev->size == mergedOffset) {
                // Новый блок вплотную примыкает к левому. Объединяем!
                mergedOffset = itPrev->offset;
                mergedSize += itPrev->size;

                // Удаляем левого соседа из freeMacroBlocksBySize. 
                // Чтобы удалить ровно его из multiset, ищем точный итератор:
                auto itSize = freeMacroBlocksBySize.find(*itPrev);
                if (itSize != freeMacroBlocksBySize.end()) {
                    freeMacroBlocksBySize.erase(itSize);
                }

                // Удаляем из основного сета
                freeMacroBlocks.erase(itPrev);
            }
        }

        // 2. Проверяем правого соседа (блок после нового)
        // Важно: itNext мог обновиться или стать невалидным, поэтому ищем его заново по актуальному mergedOffset + mergedSize
        itNext = freeMacroBlocks.lower_bound({ mergedOffset + mergedSize, 0 });
        if (itNext != freeMacroBlocks.end() && itNext->offset == mergedOffset + mergedSize) {
            // Правый сосед примыкает вплотную к концу нашего объединенного блока
            mergedSize += itNext->size;

            // Удаляем правого соседа из freeMacroBlocksBySize по итератору
            auto itSize = freeMacroBlocksBySize.find(*itNext);
            if (itSize != freeMacroBlocksBySize.end()) {
                freeMacroBlocksBySize.erase(itSize);
            }

            // Удаляем из основного сета
            freeMacroBlocks.erase(itNext);
        }

        // 3. Добавляем итоговый объединенный блок в оба контейнера
        MemoryBlock merged = { mergedOffset, mergedSize };
        freeMacroBlocks.insert(merged);
        freeMacroBlocksBySize.insert(merged);
    }

    void RemoveFreeBlock(uint32_t offset, uint32_t size) {
        MemoryBlock block = { offset, size };
        freeMacroBlocks.erase(block);

        auto it = freeMacroBlocksBySize.find(block);
        if (it != freeMacroBlocksBySize.end()) {
            freeMacroBlocksBySize.erase(it);
        }
    }



    VoxelManager(uint32_t maxBricksInPool);

    void SetGpuBuffers(uint32_t macroBuffer, uint32_t poolBuffer, uint32_t metaBuffer,
        uint32_t materialsBuffer, uint32_t chunkBuffer, uint32_t bvhBuffer);

    void UploadBVH();

    uint32_t GetMetaBufferID()      const { return gpuMetaBufferID; }
    uint32_t GetChunkBufferID()      const { return gpuChunkBufferID; }
    uint32_t GetBrickPoolBufferID() const { return gpuBrickPoolBufferID; }
    uint32_t GetMacroGridBufferID() const { return gpuMacroGridBufferID; }
    uint32_t GetMaterialsBufferID() const { return gpuMaterialsBufferID; }
    uint32_t GetBvhBufferID()       const { return gpuBvhBufferID; }

    void LoadWorldBasePalette(const std::vector<Material>& baseMaterials);
    
    VoxelObject& GetObject(uint32_t id);

    Chunk& GetChunk(uint32_t id)
    {
        auto it = activeChunks.find(id);
        if (it == activeChunks.end()) {
            static Chunk dummy;
            return dummy;
        }
        return it->second;
    };

    uint32_t GetChunk(glm::ivec3 pos)
    {
        auto it = chunkIDs.find(pos);
        if (it == chunkIDs.end()) {
            static uint32_t dummy;
            return dummy;
        }
        return it->second;
    };

    const VoxelObject& GetObject(uint32_t id) const;

    uint32_t RegisterObject(VoxelObject&& obj);
    
     
    void UnregisterObject(uint32_t objId);
    void UnregisterChunk(uint32_t objId);
    
    void OnObjectVoxelsChanged(uint32_t objId, int voxelX, int voxelY, int voxelZ);

    void Update(float dt);

    // Сигнатура теперь принимает unique_ptr по значению через move
    void InitiateChunkRegistration(std::unique_ptr<Chunk> obj);

    void ProcessChunkData(uint32_t objId, Chunk& obj, uint32_t macroOffset, std::vector<uint32_t>& freeBrickIndicesPool, ChunkProcessingResult& result);
    
    void UpdatePendingChunksGpu();

    void SetDataInChunkTex(int globalX, int globalY, int globalZ, uint32_t data, GLuint tex);

    int getTextureCoord(int globalCoord);

    
    std::vector<uint32_t> GetAllObjectIds() const;
    uint32_t MaxObjectsEverCreated() { return maxObjectsEverCreated; };
    uint32_t MaxChunksEverCreated() { return maxChunksEverCreated; };
    
    int fmod_cyclical(int a, int b) {
        int r = a % b;
        return r < 0 ? r + b : r;
    }

    int BuildChunkBVH(const std::vector<uint32_t>& objects, glm::ivec3 chunkPos);

    uint32_t BuildRecursive(const std::vector<uint32_t>& objects, std::vector<size_t>& indices, size_t from, size_t to);
    
    void UpdateBVH();
    void ClearAllBVH();
    void ClearAll();
    void SyncGpuAfterClear();
};

    
