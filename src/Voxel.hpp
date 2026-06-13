#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <unordered_map>



struct Voxel {
    uint8_t ID = 0;

    bool IsEmpty() const
    {
        return ID == 0;
    }
};
struct GpuBvhNode {
    glm::vec4 boxMin = glm::vec4(0.0f);    // .xyz = Мировой AABB min, .w = Индекс левого сына
    glm::vec4 boxMax = glm::vec4(0.0f);    // .xyz = Мировой AABB max, .w = Индекс правого сына
    glm::ivec4 metaData = glm::ivec4(-1);  // .x = Индекс объекта в MetaBuffer (если лист, иначе -1)
}; // Ровно 48 байт

#pragma pack(push, 1)
struct  Material
{
    uint8_t Color[3] = { 0, 0, 0 };
    uint8_t MetalFuzziness = 255;
    float Emission = 0.0f;

    // Возвращаем структуру из двух 32-битных чисел вместо одного 64-битного
    glm::uvec2 GetEncoded() const {
        uint32_t part1 = 0; // Сюда упакуем Color и Emission
        uint32_t part2 = 0; // Сюда упакуем Fuzziness

        // 16 бит на RGB565 цвет
        part1 |= (uint32_t)Color[0] >> (8 - 5) << 11;
        part1 |= (uint32_t)Color[1] >> (8 - 6) << 5;
        part1 |= (uint32_t)Color[2] >> (8 - 5) << 0;

        // Еще 16 бит на Emission через packHalf2x16
        // Поскольку packHalf2x16 дает 32 бита, а нам нужны только старшие 16 (там лежит Emission)
        uint32_t packedHalf = glm::packHalf2x16(glm::vec2(0.0f, Emission));
        part1 |= (packedHalf & 0xFFFF0000u); // Объединяем в один uint32 (16 бит цвет + 16 бит emission)

        // Во вторую половинку пишем Fuzziness
        part2 |= (uint32_t)MetalFuzziness;

        return glm::uvec2(part1, part2);
    }
};
#pragma pack(pop)



struct Brick {
    static constexpr int Size = 8;
    static constexpr int NumVoxels = Size * Size * Size;

    Voxel Data[NumVoxels] = { 0 };

    static uint32_t GetLinearIndex(int x, int y, int z) {
        return (x & 7) | ((y & 7) << 3) | ((z & 7) << 6);
    }

    bool IsEmpty() const {
        for (int i = 0; i < NumVoxels; i++) {
            if (Data[i].ID != 0) return false;
        }
        return true;
    }
};

template<int ShiftXZ_, int ShiftY_, bool Signed_>
struct LinearIndexer3D {
    static const int32_t SizeXZ = 1 << ShiftXZ_, SizeY = 1 << ShiftY_;
    static const int32_t MaskXZ = SizeXZ - 1, MaskY = SizeY - 1;

    static uint32_t GetIndex(int32_t x, int32_t y, int32_t z) {
        return (x & MaskXZ) | (z & MaskXZ) << ShiftXZ_ | (y & MaskY) << (ShiftXZ_ * 2);
    }
};

using MaskIndexer = LinearIndexer3D<2, 2, false>;
using BrickIndexer = LinearIndexer3D<3, 3, false>;
using WorldSectorIndexer = LinearIndexer3D<12, 8, true>;

struct Sector {
    std::vector<Brick> Storage;
    uint8_t BrickSlots[64] = { 0 };

    Brick* GetBrick(int32_t localBrickX, int32_t localBrickY, int32_t localBrickZ, bool create = false) {
        uint32_t slotIdx = MaskIndexer::GetIndex(localBrickX, localBrickY, localBrickZ);
        uint8_t storageIdx = BrickSlots[slotIdx];

        if (storageIdx != 0) {
            return &Storage[storageIdx - 1];
        }

        if (create) {
            Storage.push_back(Brick());
            BrickSlots[slotIdx] = static_cast<uint8_t>(Storage.size());
            return &Storage.back();
        }
        return nullptr;
    }
};

struct VoxelMap {
    std::unordered_map<uint32_t, Sector> Sectors;
    Material materials[256];

    glm::uvec3 size;

    static uint32_t GetSectorKey(int32_t sectorX, int32_t sectorY, int32_t sectorZ) {
        return (static_cast<uint32_t>(sectorX) & 0x3FF) |
            ((static_cast<uint32_t>(sectorZ) & 0x3FF) << 10) |
            ((static_cast<uint32_t>(sectorY) & 0x3FF) << 20);
    }

    Voxel GetVoxel(int32_t gx, int32_t gy, int32_t gz) const {
        int32_t sectorX = gx >> 5;
        int32_t sectorY = gy >> 5;
        int32_t sectorZ = gz >> 5;

        Voxel v;

        uint32_t key = GetSectorKey(sectorX, sectorY, sectorZ);
        auto it = Sectors.find(key);
        if (it == Sectors.end()) return v;

        const Sector& sector = it->second;

        int32_t lbx = (gx >> 3) & 3;
        int32_t lby = (gy >> 3) & 3;
        int32_t lbz = (gz >> 3) & 3;

        uint32_t slotIdx = MaskIndexer::GetIndex(lbx, lby, lbz);
        uint8_t storageIdx = sector.BrickSlots[slotIdx];
        if (storageIdx == 0) return v;

        const Brick& brick = sector.Storage[storageIdx - 1];

        int32_t vx = gx & 7;
        int32_t vy = gy & 7;
        int32_t vz = gz & 7;

        uint16_t voxelIdx = Brick::GetLinearIndex(vx, vy, vz);

        v.ID = brick.Data[voxelIdx].ID;
        return v;
    }

    void SetVoxel(int32_t gx, int32_t gy, int32_t gz, uint8_t color565) {
        int32_t sectorX = gx >> 5;
        int32_t sectorY = gy >> 5;
        int32_t sectorZ = gz >> 5;

        uint32_t key = GetSectorKey(sectorX, sectorY, sectorZ);
        Sector& sector = Sectors[key];

        int32_t lbx = (gx >> 3) & 3;
        int32_t lby = (gy >> 3) & 3;
        int32_t lbz = (gz >> 3) & 3;

        Brick* brick = sector.GetBrick(lbx, lby, lbz, true);

        int32_t vx = gx & 7;
        int32_t vy = gy & 7;
        int32_t vz = gz & 7;

        uint32_t voxelIdx = Brick::GetLinearIndex(vx, vy, vz);
        brick->Data[voxelIdx].ID = color565;
    }
};

