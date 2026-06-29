#pragma once

#include <unordered_map>
#include <vector>
#include <cmath>
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>

#ifndef GLM_FORCE_CXX11
#define GLM_FORCE_CXX11
#endif

class StaticGrid {
public:
    static constexpr int GRID_SIZE = 64;
    int viewdist = 3;

    static constexpr float VOXEL_SIZE = 0.1f;
    static constexpr float CHUNK_WORLD_SIZE = 64.0f * VOXEL_SIZE;

    VoxelManager* manager;

    std::vector<Material> materials;
     

    StaticGrid(VoxelManager* voxelManager, int rDistance = 12) : manager(voxelManager) 
    {
        materials.resize(256);

        materials[1] = { {34, 139, 34}, 0, 0.0f };

        materials[2] = { {101, 67, 33}, 0, 0.0f };

        materials[3] = { {128, 128, 128}, 50, 0.0f };

        materials[4] = { {30, 144, 255}, 200, 1.0f }; 

        if (!manager) return;
        manager->LoadWorldBasePalette(materials);
    }
    void UpdateChunks(glm::vec3 cameraPos) {
        // 1. Считаем текущую позицию камеры в координатах СЕТКИ ЧАНКОВ.
        // Делим позицию в метрах на физический размер чанка в метрах (6.4f).
        int centerChunkX = std::floor(cameraPos.x / CHUNK_WORLD_SIZE);
        int centerChunkZ = std::floor(cameraPos.z / CHUNK_WORLD_SIZE);
        int centerChunkY = std::floor(cameraPos.y / CHUNK_WORLD_SIZE);

        

        // 2. ЭТАП ЗАГРУЗКИ: Создаем новые чанки вокруг камеры
        for (int y = -viewdist; y <= viewdist; y++)

        for (int z = -viewdist; z <= viewdist; z++) {
            for (int x = -viewdist; x <= viewdist; x++) {
                glm::ivec3 chunkPos(centerChunkX + x, centerChunkY + y, centerChunkZ + z);
                LoadChunk(chunkPos);
            }
        }

        // 3. ЭТАП ВЫГРУЗКИ: Собираем чанки, которые вылетели за пределы viewdist
        std::vector<glm::ivec3> chunksToRemove;

        for (const auto& pair : manager->chunkIDs) {
            // pair.first — это КЛЮЧ мапы, то есть логическая координата чанка в сетке (например: {2, 0, -1})
            glm::ivec3 cPos = pair.first;

            // Считаем расстояние в чанках
            int distX = std::abs(cPos.x - centerChunkX);
            int distZ = std::abs(cPos.z - centerChunkZ);

            // Если чанк вышел за дистанцию видимости + 1 (буфер стабильности)
            if (distX > (viewdist + 1) || distZ > (viewdist + 1)) {
                chunksToRemove.push_back(cPos);
            }
        }

        // Физически удаляем старые чанки
        for (const auto& pos : chunksToRemove) {
            manager->UnregisterChunk(manager->GetChunk(pos));
        }
    }


    void LoadChunk(glm::ivec3 pos)
    {
        if (manager->GetChunk(pos)) return;
        
        for (const auto& loadingPos : manager->chunksInGeneration) {
            if (loadingPos == pos) return; 
        }
        
        CreateChunk(pos);
        
        
    }

    
    void CreateChunk(glm::ivec3 GlobalPos)
{
    auto c = std::make_unique<Chunk>();
    c->pos = GlobalPos;

 

    // Циклы теперь просто заполняют воксели до одной фиксированной высоты targetWorldHeight
    //for (int x = 0; x < GRID_SIZE; x++)
    //{
    //    for (int z = 0; z < GRID_SIZE; z++)
    //    {
    //        for (int y = 0; y < GRID_SIZE; y++)
    //        {
    //            int worldY = fabs(GlobalPos.z+ GlobalPos.x);

    //            if(y<= worldY)
    //                c->voxelMap.SetVoxel(x, y, z, 1);
    //          
    //        }
    //    }
    //}

    // Генерация только ребер куба размером GRID_SIZE x GRID_SIZE x GRID_SIZE
    for (int x = 0; x < GRID_SIZE; x++)
    {
        for (int y = 0; y < GRID_SIZE; y++)
        {
            for (int z = 0; z < GRID_SIZE; z++)
            {
                // Проверяем, находится ли точка на ребре куба
                bool onEdge = false;
                  //            // Проверяем X-грани (x == 0 или x == GRID_SIZE-1)
                bool onXEdge = (x == 0 || x == GRID_SIZE - 1);

               // Проверяем Y-грани (y == 0 или y == GRID_SIZE-1)
                bool onYEdge = (y == 0 || y == GRID_SIZE - 1);

                // Проверяем Z-грани (z == 0 или z == GRID_SIZE-1)
                bool onZEdge = (z == 0 || z == GRID_SIZE - 1);

                // Точка на ребре, если как минимум две координаты находятся на границах
                int edgeCount = 0;
               if (onXEdge) edgeCount++;
                if (onYEdge) edgeCount++;
                if (onZEdge) edgeCount++;

                onEdge = (edgeCount >= 2);
                            if (onEdge)
                    c->voxelMap.SetVoxel(x, y, z, 1);
            }
        }
    }
    manager->InitiateChunkRegistration(std::move(c));
}


};