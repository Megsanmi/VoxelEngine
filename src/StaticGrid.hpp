#pragma once
#include "Voxel.hpp"
#include <vector>
#include <glm/glm.hpp>
#include <functional>
#include <algorithm>

struct Chunk {
    glm::ivec3 pos;           // Позиция чанка в мировых координатах
    std::vector<uint8_t> voxels; // 1D массив вокселей (ID материалов)
    bool dirty = true;        // Нужно обновить GPU
    uint32_t macroOffset = 0; // Смещение в ленте макросеток
    uint32_t brickOffset = 0; // Смещение в ленте бриков
    uint32_t matOffset = 0;   // Смещение в ленте материалов

    static constexpr int SIZE = 32; // Размер чанка (32? = 32768 вокселей)
    static constexpr int HEIGHT = 1000; // Размер чанка (32? = 32768 вокселей)

    Chunk(glm::ivec3 position) : pos(position) {
        voxels.resize(SIZE * SIZE * SIZE, 0);
    }

    int GetIndex(int x, int y, int z) const {
        return x + z * SIZE + y * SIZE * SIZE;
    }

    uint8_t GetVoxel(int x, int y, int z) const {
        if (x < 0 || x >= SIZE || y < 0 || y >= SIZE || z < 0 || z >= SIZE)
            return 0;
        return voxels[GetIndex(x, y, z)];
    }

    void SetVoxel(int x, int y, int z, uint8_t id) {
        if (x < 0 || x >= SIZE || y < 0 || y >= SIZE || z < 0 || z >= SIZE)
            return;
        voxels[GetIndex(x, y, z)] = id;
        dirty = true;
    }

    bool IsEmpty() const {
        for (auto v : voxels) if (v != 0) return false;
        return true;
    }
};
struct RawNode {
    uint64_t ChildMask = 0;
    uint32_t ChildPtr = 0;
};

class treeBuilder
{
public:
    // Конструктор теперь принимает константную ссылку
    treeBuilder(const Chunk& chunk, std::vector<RawNode>& buffer)
        : chunk(chunk), nodeBuffer(buffer)
    {
    }

    uint32_t Build(glm::ivec3 chunkMinPos) {
        return BuildNode(chunkMinPos.x, chunkMinPos.y, chunkMinPos.z, 0);
    }

private:
    // ИСПРАВЛЕНО: добавлено const, чтобы исправить ошибку компиляции E0433
    const Chunk& chunk;
    std::vector<RawNode>& nodeBuffer;

    uint32_t ConvertColor(uint16_t color565) {
        if (color565 == 0) return 0;
        uint32_t r = ((color565 >> 11) & 0x1F) * 255 / 31;
        uint32_t g = ((color565 >> 5) & 0x3F) * 255 / 63;
        uint32_t b = (color565 & 0x1F) * 255 / 31;
        return (r << 24) | (g << 16) | (b << 8) | 0xFF;
    }

    uint32_t BuildNode(int x, int y, int z, int step)
    {
        // Вычисляем размер текущего куба на данном шаге вложенности
        // step 0 = 64, step 1 = 16, step 2 = 4, step 3 = 1
        int currentSize = 64 >> (step * 2);

        // -----------------------------------------------------------------
        // ОПТИМИЗАЦИЯ: Раннее схлопывание однородных нод (Early Out)
        // -----------------------------------------------------------------
        Voxel firstVoxel = chunk.GetVoxel(x, y, z);
        bool isUniform = true;

        for (int dz = 0; dz < currentSize; dz++) {
            for (int dy = 0; dy < currentSize; dy++) {
                for (int dx = 0; dx < currentSize; dx++) {
                    if (chunk.GetVoxel(x + dx, y + dy, z + dz).ID != firstVoxel.ID) {
                        isUniform = false;
                        break;
                    }
                }
                if (!isUniform) break;
            }
            if (!isUniform) break;
        }

        // Если весь этот подкуб состоит из одного типа вокселей (например, воздух или монолитный камень)
        if (isUniform) {
            if (firstVoxel.IsEmpty()) {
                return 0xFFFFFFFF; // Воздух (пустота)
            }

            // Если это сплошной цветной куб, создаем для него одну листовую ноду
            RawNode uniformLeaf;
            uniformLeaf.ChildMask = 0; // Лист
            uniformLeaf.ChildPtr = ConvertColor(firstVoxel.ID);

            uint32_t nodeIndex = static_cast<uint32_t>(nodeBuffer.size());
            nodeBuffer.push_back(uniformLeaf);
            return nodeIndex;
        }

        // Базовый случай: мы дошли до минимального размера вокселя (1x1x1)
        if (step == 3)
        {
            if (firstVoxel.IsEmpty()) {
                return 0xFFFFFFFF;
            }

            RawNode leafNode;
            leafNode.ChildMask = 0;
            leafNode.ChildPtr = ConvertColor(firstVoxel.ID);

            uint32_t nodeIndex = static_cast<uint32_t>(nodeBuffer.size());
            nodeBuffer.push_back(leafNode);
            return nodeIndex;
        }

        // -----------------------------------------------------------------
        // Рекурсивный обход детей (сетка 4х4х4 = 64 ребенка)
        // -----------------------------------------------------------------
        int childSize = currentSize / 4;

        // Временный массив для хранения результатов вызовов детей
        RawNode localChildren[64];
        uint32_t localChildIndices[64];
        int activeChildrenCount = 0;
        uint64_t mask = 0;

        int bitIdx = 0;
        for (int dz = 0; dz < 4; dz++) {
            for (int dy = 0; dy < 4; dy++) {
                for (int dx = 0; dx < 4; dx++) {
                    int cx = x + dx * childSize;
                    int cy = y + dy * childSize;
                    int cz = z + dz * childSize;

                    uint32_t childIdx = BuildNode(cx, cy, cz, step + 1);

                    if (childIdx != 0xFFFFFFFF) {
                        mask |= (1ULL << bitIdx);
                        // Запоминаем саму созданную ноду ребенка
                        localChildren[activeChildrenCount] = nodeBuffer[childIdx];
                        activeChildrenCount++;

                        // Удаляем ребенка из конца буфера, так как мы переупакуем его ниже
                        nodeBuffer.pop_back();
                    }
                    bitIdx++;
                }
            }
        }

        // Если все дети оказались пустыми (воздух)
        if (activeChildrenCount == 0) {
            return 0xFFFFFFFF;
        }

        // Выделяем память в глобальном буфере под активных детей С ПЛОШНЫМ БЛОКОМ
        uint32_t firstChildPtr = static_cast<uint32_t>(nodeBuffer.size());
        for (int i = 0; i < activeChildrenCount; i++) {
            nodeBuffer.push_back(localChildren[i]);
        }

        // Создаем родительскую ноду, указывающую на этот блок детей
        RawNode parentNode;
        parentNode.ChildMask = mask;
        parentNode.ChildPtr = firstChildPtr;

        uint32_t parentIndex = static_cast<uint32_t>(nodeBuffer.size());
        nodeBuffer.push_back(parentNode);

        return parentIndex;
    }
};
class StaticGrid {
public:
    std::vector<Chunk> chunks;
    int chunkSize = Chunk::SIZE;

    // Палитра материалов для всего террайна
    Material materials[256];

    StaticGrid() {
        // Материалы по умолчанию
        memset(materials, 0, sizeof(materials));

        // Трава
        materials[1] = { {34, 139, 34}, 0, 0.0f };
        // Земля
        materials[2] = { {101, 67, 33}, 0, 0.0f };
        // Камень
        materials[3] = { {128, 128, 128}, 50, 0.0f };
        // Вода
        materials[4] = { {30, 144, 255}, 200, 1.0f };
    }

    // Получение чанка по мировым координатам
    Chunk* GetChunk(int worldX, int worldY, int worldZ) {
        int cx = worldX / chunkSize;
        int cy = worldY / chunkSize;
        int cz = worldZ / chunkSize;

        for (auto& chunk : chunks) {
            if (chunk.pos.x == cx && chunk.pos.y == cy && chunk.pos.z == cz)
                return &chunk;
        }
        return nullptr;
    }

    // Получение или создание чанка
    Chunk* GetOrCreateChunk(int worldX, int worldY, int worldZ) {
        int cx = worldX / chunkSize;
        int cy = worldY / chunkSize;
        int cz = worldZ / chunkSize;

        Chunk* existing = GetChunk(worldX, worldY, worldZ);
        if (existing) return existing;

        chunks.emplace_back(glm::ivec3(cx, cy, cz));
        return &chunks.back();
    }

    // Получение вокселя
    uint8_t GetVoxel(int worldX, int worldY, int worldZ) {
        Chunk* chunk = GetChunk(worldX, worldY, worldZ);
        if (!chunk) return 0;

        int localX = worldX - chunk->pos.x * chunkSize;
        int localY = worldY - chunk->pos.y * chunkSize;
        int localZ = worldZ - chunk->pos.z * chunkSize;

        return chunk->GetVoxel(localX, localY, localZ);
    }

    // Установка вокселя
    void SetVoxel(int worldX, int worldY, int worldZ, uint8_t id) {
        Chunk* chunk = GetOrCreateChunk(worldX, worldY, worldZ);

        int localX = worldX - chunk->pos.x * chunkSize;
        int localY = worldY - chunk->pos.y * chunkSize;
        int localZ = worldZ - chunk->pos.z * chunkSize;

        chunk->SetVoxel(localX, localY, localZ, id);
    }

    // Генерация высотной карты
    void GenerateTerrain(int width, int height, int depth) {
        chunks.clear();

        for (int cx = 0; cx < width / chunkSize; cx++) {
            for (int cz = 0; cz < depth / chunkSize; cz++) {
                Chunk chunk(glm::ivec3(cx, 0, cz));

                for (int x = 0; x < chunkSize; x++) {
                    for (int z = 0; z < chunkSize; z++) {
                        int worldX = cx * chunkSize + x;
                        int worldZ = cz * chunkSize + z;

                        // Простая высотная карта
                        float height = sin(worldX * 0.1f) * cos(worldZ * 0.1f) * 10.0f + 15.0f;
                        height += sin(worldX * 0.05f + worldZ * 0.07f) * 5.0f;

                        for (int y = 0; y < chunkSize; y++) {
                            int worldY = y; // Все чанки на уровне 0

                            if (worldY < height - 3) {
                                chunk.SetVoxel(x, y, z, 3); // Камень
                            }
                            else if (worldY < height) {
                                chunk.SetVoxel(x, y, z, 2); // Земля
                            }
                            else if (worldY == (int)height && height > 10) {
                                chunk.SetVoxel(x, y, z, 1); // Трава
                            }
                            else if (worldY < 8) {
                                chunk.SetVoxel(x, y, z, 4); // Вода
                            }
                        }
                    }
                }

                if (!chunk.IsEmpty()) {
                    chunks.push_back(chunk);
                }
            }
        }
    }

    // Подсчёт грязных чанков
    int GetDirtyChunkCount() const {
        int count = 0;
        for (auto& c : chunks) if (c.dirty) count++;
        return count;
    }

    // Сбор всех материалов
    void CollectMaterials(std::vector<Material>& outMaterials) {
        outMaterials.clear();
        outMaterials.insert(outMaterials.end(), std::begin(materials), std::end(materials));
    }
};