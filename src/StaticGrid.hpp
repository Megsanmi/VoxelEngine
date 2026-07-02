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
    int viewdist = 5;

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
 
        int centerChunkX = std::floor(cameraPos.x / CHUNK_WORLD_SIZE);
        int centerChunkZ = std::floor(cameraPos.z / CHUNK_WORLD_SIZE);
        int centerChunkY = std::floor(cameraPos.y / CHUNK_WORLD_SIZE);

 
        for (int z = -viewdist; z <= viewdist; z++) {
            for (int x = -viewdist; x <= viewdist; x++) {
                glm::ivec3 chunkPos(centerChunkX + x, 0, centerChunkZ + z);
                LoadChunk(chunkPos);
            }
        }
         
        std::vector<glm::ivec3> chunksToRemove;

        for (const auto& pair : manager->chunkIDs) {
            glm::ivec3 cPos = pair.first;

            int distX = std::abs(cPos.x - centerChunkX);
            int distZ = std::abs(cPos.z - centerChunkZ);

            if (distX > (viewdist + 1) || distZ > (viewdist + 1)) {
                chunksToRemove.push_back(cPos);
            }
        }

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
    for (int x = 0; x < GRID_SIZE; x++)
    {
        for (int z = 0; z < GRID_SIZE; z++)
        {
            for (int y = 0; y < GRID_SIZE; y++)
            {
                int worldY = (sin(float(GlobalPos.z + z) / 10.f) + sin(float(GlobalPos.x + x) / 10.f)) * 5;

                if (y <= worldY + 10)
                    c->voxelMap.SetVoxel(x, y, z, 1);

            }
        }
    }

   
    manager->InitiateChunkRegistration(std::move(c));
}


};