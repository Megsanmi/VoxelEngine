#version 430 core

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

int MICRO_STEPS = 24;
int STEPS = 100;

const int TEXTURE_SIZE = 64;
const int CHUNK_SIZE_VOXELS = 64;
const float VOXEL_SIZE = 0.1f;
const float CHUNK_SIZE_METERS = 6.4f;

const float CHUNK_LOCAL_BOUND = float(CHUNK_SIZE_VOXELS); 
// ==========================================
// СТРУКТУРЫ ДАННЫХ И БУФЕРЫ 
// ==========================================

struct GpuEntityMeta {
    mat4 invModelMatrix;
    mat4 modelMatrix;
    vec4 boxMin;        // w не используется
    vec4 boxMax;        // w не используется

    ivec4 sizeInBricks; // x, y, z - размеры, w - macroOffset в ленте макро-сеток
    ivec4 sizeInVoxels; // x, y, z - размеры, w - matOffset в ленте материалов
};

struct ChunkMeta{
    ivec4 pos; //w - offset  
    uint BVHnode;
};

struct BVHNode {
    vec4 boxMin; // .w = индекс левого ребенка
    vec4 boxMax; // .w = индекс правого ребенка
    ivec4 metaData; // .x = objectIndex (если лист, иначе -1)
};

struct Brick {
    uint voxels[128]; // 512 байт (1 воксель = 1 байт)
};

struct GpuMaterial {
    uint packedColorAndFuzz; // R (8 бит), G (8 бит), B (8 бит), Fuzz (8 бит)
    float emission;          // 4 байта
};

layout(std430, binding = 0) readonly buffer MetaBuffer {
    GpuEntityMeta entities[]; // Метаданные объектов
};

layout(std430, binding = 1) readonly buffer MacroGridBuffer {
    uint macroGridLenta[]; // Единая лента макро-сеток всех объектов
};

layout(std430, binding = 2) readonly buffer BrickStorageBuffer {
    Brick brickLenta[]; // Единая лента бриков всех объектов
};

layout(std430, binding = 3) readonly buffer MaterialBuffer {
    GpuMaterial materialsLenta[]; // Единая лента материалов
};

layout(std430, binding = 4) readonly buffer BvhBuffer {
    BVHNode bvhNodes[];
};

layout(std430, binding = 5) readonly buffer chunkBuffer {
    ChunkMeta chunks[];
};

layout(binding = 7) uniform usampler3D worldChunkMap;
layout(binding = 8) uniform usampler3D bvhMap;


layout(rgba8, binding = 0) uniform writeonly image2D imgOutput;

// ==========================================
// ЮНИФОРМЫ КАМЕРЫ И СЦЕНЫ
// ==========================================
uniform mat4 viewMatrix;
uniform vec3 cameraPos;
uniform vec3 lightDir = vec3(0.5, 0.6, 0.7);
uniform float time;
uniform int NumEntities = 2; // Передаем общее количество объектов: objects.size()
uniform int NumChunks = 2; 
uniform bool debugFlag = false; 

// ==========================================
// ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
// ==========================================

// Пересечение луча с AABB (Алгоритм Смита)
bool intersectAABB(vec3 ro, vec3 rd, vec3 boxMin, vec3 boxMax, out float tMin, out float tMax) {
    vec3 invRd = 1.0 / (rd + vec3(1e-9));
    vec3 t0 = (boxMin - ro) * invRd;
    vec3 t1 = (boxMax - ro) * invRd;
    
    vec3 tmin3 = min(t0, t1);
    vec3 tmax3 = max(t0, t1);
    
    tMin = max(max(tmin3.x, tmin3.y), tmin3.z);
    tMax = min(min(tmax3.x, tmax3.y), tmax3.z);
    
    return tMin <= tMax && tMax > 0.0;
}

// Распаковка материала (RGBA + Fuzz) из 8-байтового формата с учетом глобального matOffset
void UnpackMaterial(uint matGlobalIdx, out vec3 color, out float fuzz, out float emission) {
    GpuMaterial mat = materialsLenta[matGlobalIdx];
    
    float r = float((mat.packedColorAndFuzz >> 0u) & 0xFFu) / 255.0;
    float g = float((mat.packedColorAndFuzz >> 8u) & 0xFFu) / 255.0;
    float b = float((mat.packedColorAndFuzz >> 16u)  & 0xFFu) / 255.0;
    
    color = vec3(r, g, b);
    fuzz = float(mat.packedColorAndFuzz & 0xFFu) / 255.0;
    emission = mat.emission;
}

// Извлечение ID вокселя из ленты бриков
uint getVoxelID(uint brickIdx, int vx, int vy, int vz) {
    // Формула: (x & 7) | ((y & 7) << 3) | ((z & 7) << 6)
    uint linearIdx = uint(vx | (vy << 3) | (vz << 6));
    
    uint uintIdx = linearIdx >> 2u; 
    uint byteOffset = (linearIdx & 3u) * 8u;
    
    return (brickLenta[brickIdx].voxels[uintIdx] >> byteOffset) & 0xFFu;
}

vec3 getChunkDeltaT(vec3 rd) {
    return vec3(
        abs(rd.x) > 1e-6 ? abs(1.0 / rd.x) : 1e30,
        abs(rd.y) > 1e-6 ? abs(1.0 / rd.y) : 1e30,
        abs(rd.z) > 1e-6 ? abs(1.0 / rd.z) : 1e30
    );
}   

// ==========================================
// ЛОКАЛЬНЫЙ ТРАССИРОВЩИК (DDA) ДЛЯ КОНКРЕТНОГО ОБЪЕКТА
// ==========================================
bool isPixelOnAABBRidge(vec3 localPos, vec3 boxSize, float thickness) {
    // Проверяем близость к границам [0] и [boxSize] по каждой оси
    bvec3 nearMin = lessThan(localPos, vec3(thickness));
    bvec3 nearMax = greaterThan(localPos, boxSize - vec3(thickness));
    
    // Считаем, сколько осей одновременно находятся у края
    int edgeCount = 0;
    if (nearMin.x || nearMax.x) edgeCount++;
    if (nearMin.y || nearMax.y) edgeCount++;
    if (nearMin.z || nearMax.z) edgeCount++;
    
    // Если точка находится на стыке хотя бы двух плоскостей — это ребро куба!
    return edgeCount >= 2;
}
// Функция знака, которая никогда не возвращает ноль (нужна для корректного шага)
float pssign(float x) {
    return x >= 0.0 ? 1.0 : -1.0;
}

bool traceVoxelObject(GpuEntityMeta meta, vec3 worldRo, vec3 worldRd, inout float closestT, out uint hitMaterialID, out vec3 normal) {
    normal = vec3(0.0);

    // 1. ОБОЛОЧКА НА ВХОДЕ: Перевод луча в локальное пространство объекта
    vec3 ro = vec3(meta.invModelMatrix * vec4(worldRo, 1.0));
    vec3 rd = vec3(meta.invModelMatrix * vec4(worldRd, 0.0)); 
    
    // Считаем масштаб изменения длины луча из-за масштабирования матрицы
    float rayScale = length(rd);
    vec3 rd_norm = rd / rayScale;

    float tMin, tMax;
    vec3 maxBounds = vec4(meta.sizeInVoxels).xyz;
    
    if (!intersectAABB(ro, rd_norm, vec3(0.0), maxBounds, tMin, tMax)) return false;

    // Переводим мировой closestT в шкалу локального нормализованного луча rd_norm
    float localClosestT = closestT * rayScale;
    
    float t = max(tMin, 0.0);
    if (t >= localClosestT) return false;
    
    vec3 pos = ro + rd_norm * t;

    ivec3 brickSize = meta.sizeInBricks.xyz;
    uint macroOffset = uint(meta.sizeInBricks.w);
    uint matOffset = uint(meta.sizeInVoxels.w); 
    
    ivec3 step = ivec3(
        rd_norm.x >= 0.0 ? 1 : -1,
        rd_norm.y >= 0.0 ? 1 : -1,
        rd_norm.z >= 0.0 ? 1 : -1
    );

    vec3 safeRd = vec3(
        abs(rd_norm.x) < 1e-6 ? (rd_norm.x >= 0.0 ? 1e-6 : -1e-6) : rd_norm.x,
        abs(rd_norm.y) < 1e-6 ? (rd_norm.y >= 0.0 ? 1e-6 : -1e-6) : rd_norm.y,
        abs(rd_norm.z) < 1e-6 ? (rd_norm.z >= 0.0 ? 1e-6 : -1e-6) : rd_norm.z
    );

    // ТВОЯ ЧАНКОВАЯ ЛОГИКА: vDeltaT на 1 воксель, deltaT на 1 брик (8 вокселей)
    vec3 vDeltaT = abs(1.0 / safeRd);
    vec3 deltaT = vDeltaT * 8.0; 

    ivec3 brickPos = ivec3(floor(pos / 8.0));
    brickPos = clamp(brickPos, ivec3(0), brickSize - ivec3(1));

    // Настройка sideT для макро-сетки
    vec3 sideT;
    sideT.x = (step.x > 0) ? (float(brickPos.x + 1) * 8.0 - pos.x) * vDeltaT.x : (pos.x - float(brickPos.x) * 8.0) * vDeltaT.x;
    sideT.y = (step.y > 0) ? (float(brickPos.y + 1) * 8.0 - pos.y) * vDeltaT.y : (pos.y - float(brickPos.y) * 8.0) * vDeltaT.y;
    sideT.z = (step.z > 0) ? (float(brickPos.z + 1) * 8.0 - pos.z) * vDeltaT.z : (pos.z - float(brickPos.z) * 8.0) * vDeltaT.z;
    sideT += t;

    ivec3 mask = ivec3(0);
    bool hitFound = false;

    // --- ЦИКЛ МАКРО-DDA (ПОЛНАЯ КОПИЯ РАБОЧЕГО ЧАНКОВОГО) ---
    for(int i = 0; i < STEPS; i++) {
        if (t >= localClosestT || t >= tMax) break;

        if (brickPos.x < 0 || brickPos.x >= brickSize.x ||
            brickPos.y < 0 || brickPos.y >= brickSize.y ||
            brickPos.z < 0 || brickPos.z >= brickSize.z) {
            break;
        }
        
        uint localMacroIdx = uint(brickPos.x + (brickPos.z * brickSize.x) + (brickPos.y * (brickSize.x * brickSize.z)));
        uint brickIdx = macroGridLenta[macroOffset + localMacroIdx];

        if (brickIdx != 0u) {
            vec3 bMin = vec3(brickPos * 8);
            vec3 bMax = bMin + vec3(8.0);
            float vTMin, vTMax;
            
            if (intersectAABB(ro, rd_norm, bMin, bMax, vTMin, vTMax)) {
                float vt_entry = max(vTMin, t);
                vec3 vPos = ro + rd_norm * (vt_entry + 0.0001); 

                ivec3 voxelPos = ivec3(floor(vPos));
                voxelPos = clamp(voxelPos, brickPos * 8, brickPos * 8 + ivec3(7));
                
                vec3 vSideT;
                vSideT.x = (step.x > 0) ? (float(voxelPos.x + 1) - vPos.x) * vDeltaT.x : (vPos.x - float(voxelPos.x)) * vDeltaT.x;
                vSideT.y = (step.y > 0) ? (float(voxelPos.y + 1) - vPos.y) * vDeltaT.y : (vPos.y - float(voxelPos.y)) * vDeltaT.y;
                vSideT.z = (step.z > 0) ? (float(voxelPos.z + 1) - vPos.z) * vDeltaT.z : (vPos.z - float(voxelPos.z)) * vDeltaT.z;
                vSideT += vt_entry;

                // Железобетонный стартовый маск входа через плоскости куба (без просадки FPS)
                ivec3 vMask = ivec3(0);
                vec3 vBound = vec3(voxelPos) + vec3(step.x > 0 ? 0.0 : 1.0, step.y > 0 ? 0.0 : 1.0, step.z > 0 ? 0.0 : 1.0);
                vec3 tPlane = (vBound - ro) / safeRd;
                float maxTPlane = max(max(tPlane.x, tPlane.y), tPlane.z);
                
                if (maxTPlane == tPlane.x)      vMask = ivec3(1, 0, 0);
                else if (maxTPlane == tPlane.y) vMask = ivec3(0, 1, 0);
                else                            vMask = ivec3(0, 0, 1);

                int microSteps = 0;
                float currentVt = vt_entry;
                
                // --- МИКРО-ЦИКЛ ---
                while (microSteps++ < MICRO_STEPS) {
                    if (currentVt >= vTMax || currentVt >= localClosestT) break;
                    if ((voxelPos >> 3) != brickPos) break; 
                    
                    int vx = voxelPos.x & 7;
                    int vy = voxelPos.y & 7;
                    int vz = voxelPos.z & 7;
                    
                    uint voxelID = getVoxelID(brickIdx, vx, vy, vz);

                    if (voxelID != 0u) {
                        localClosestT = currentVt; // Записываем точное время
                        mask = vMask;
                        hitMaterialID = matOffset + voxelID;
                        hitFound = true;
                        break;
                    }
                    
                    if (vSideT.x < vSideT.y) {
                        if (vSideT.x < vSideT.z) { currentVt = vSideT.x; vSideT.x += vDeltaT.x; voxelPos.x += step.x; vMask = ivec3(1, 0, 0); }
                        else                     { currentVt = vSideT.z; vSideT.z += vDeltaT.z; voxelPos.z += step.z; vMask = ivec3(0, 0, 1); }
                    } else {
                        if (vSideT.y < vSideT.z) { currentVt = vSideT.y; vSideT.y += vDeltaT.y; voxelPos.y += step.y; vMask = ivec3(0, 1, 0); }
                        else                     { currentVt = vSideT.z; vSideT.z += vDeltaT.z; voxelPos.z += step.z; vMask = ivec3(0, 0, 1); }
                    }
                }
                
                if (hitFound) break;
                t = vTMax; // Просто переходим к выходу, макро-шаг сделает всё сам
            }
        }
        
        // Автономный шаг макро-DDA
        if (sideT.x < sideT.y) {
            if (sideT.x < sideT.z) { t = sideT.x; sideT.x += deltaT.x; brickPos.x += step.x; }
            else                     { t = sideT.z; sideT.z += deltaT.z; brickPos.z += step.z; }
        } else {
            if (sideT.y < sideT.z) { t = sideT.y; sideT.y += deltaT.y; brickPos.y += step.y; }
            else                     { t = sideT.z; sideT.z += deltaT.z; brickPos.z += step.z; }
        }
    }

    // 2. ОБОЛОЧКА НА ВЫХОДЕ: Перевод результатов работы чанковой математики в мир
    if (hitFound) {
        // Переводим локальное время обратно в мировую дистанцию
        closestT = localClosestT / rayScale;
        
        // Переводим локальную нормаль в мировую через транспонированную инверсную матрицу
        vec3 localNormal = -vec3(step) * vec3(mask);
        mat3 normalMatrix = transpose(mat3(meta.invModelMatrix));
        normal = normalize(normalMatrix * localNormal);
        return true;
    }

    return false;
}



bool traceChunkObject(ChunkMeta meta, vec3 worldRo, vec3 worldRd, inout float closestT, out uint hitMaterialID, out vec3 normal) {
    normal = vec3(0.0);
    uint macroOffset = uint(meta.pos.w);
    
    if (macroOffset == 0u) return false;

    // 1. Перевод в локальное пространство вокселей чанка (0.0 - 64.0)
    vec3 ro = (worldRo - meta.pos.xyz * 6.4) / VOXEL_SIZE;
    vec3 rd = worldRd / VOXEL_SIZE; 
    
    float rayScale = length(rd);
    vec3 rd_norm = rd / rayScale;

    float tMin, tMax;
    vec3 maxBounds = vec3(CHUNK_LOCAL_BOUND);
    
    if (!intersectAABB(ro, rd_norm, vec3(0.0), maxBounds, tMin, tMax)) return false;

    // Ограничение по closestT
    vec3 worldLimitPoint = worldRo + worldRd * closestT;
    vec3 localLimitPoint = (worldLimitPoint - meta.pos.xyz * 6.4) / VOXEL_SIZE;
    float localClosestT = dot(localLimitPoint - ro, rd_norm);
    
    float t = max(tMin, 0.0);
    if (t >= localClosestT) return false;
    
    vec3 pos = ro + rd_norm * t;

    ivec3 brickSize = ivec3(CHUNK_SIZE_VOXELS / 8); // 8x8x8 бриков
    uint matOffset = 0u; 
    
    ivec3 step = ivec3(
        rd_norm.x >= 0.0 ? 1 : -1,
        rd_norm.y >= 0.0 ? 1 : -1,
        rd_norm.z >= 0.0 ? 1 : -1
    );

    vec3 safeRd = vec3(
        abs(rd_norm.x) < 1e-6 ? (rd_norm.x >= 0.0 ? 1e-6 : -1e-6) : rd_norm.x,
        abs(rd_norm.y) < 1e-6 ? (rd_norm.y >= 0.0 ? 1e-6 : -1e-6) : rd_norm.y,
        abs(rd_norm.z) < 1e-6 ? (rd_norm.z >= 0.0 ? 1e-6 : -1e-6) : rd_norm.z
    );
    vec3 deltaT = abs(1.0 / safeRd) * 8.0; // Дельта для макро-шага (8 вокселей)

    ivec3 brickPos = ivec3(floor(pos / 8.0));
    brickPos = clamp(brickPos, ivec3(0), brickSize - ivec3(1));

    // Настройка начальных sideT для МАКРО-сетки
    vec3 sideT;
    sideT.x = (step.x > 0) ? (float(brickPos.x + 1) * 8.0 - pos.x) * (deltaT.x / 8.0) : (pos.x - float(brickPos.x) * 8.0) * (deltaT.x / 8.0);
    sideT.y = (step.y > 0) ? (float(brickPos.y + 1) * 8.0 - pos.y) * (deltaT.y / 8.0) : (pos.y - float(brickPos.y) * 8.0) * (deltaT.y / 8.0);
    sideT.z = (step.z > 0) ? (float(brickPos.z + 1) * 8.0 - pos.z) * (deltaT.z / 8.0) : (pos.z - float(brickPos.z) * 8.0) * (deltaT.z / 8.0);
    sideT += t; // Привязываем к глобальному стартовому таймеру t

    ivec3 mask = ivec3(0);
    bool hitFound = false;

    // --- ЦИКЛ МАКРО-DDA (ПО БРИКАМ) ---
    for(int i = 0; i < STEPS; i++) {
        if (t >= localClosestT || t >= tMax) break;

        if (brickPos.x < 0 || brickPos.x >= brickSize.x ||
            brickPos.y < 0 || brickPos.y >= brickSize.y ||
            brickPos.z < 0 || brickPos.z >= brickSize.z) {
            break;
        }
        
        uint localMacroIdx = uint(brickPos.x + (brickPos.z * brickSize.x) + (brickPos.y * (brickSize.x * brickSize.z)));
        uint brickIdx = macroGridLenta[macroOffset + localMacroIdx];
        
        

        if (brickIdx != 0u) {
            vec3 bMin = vec3(brickPos * 8);
            vec3 bMax = bMin + vec3(8.0);
            float vTMin, vTMax;
            
            if (intersectAABB(ro, rd_norm, bMin, bMax, vTMin, vTMax)) {
                float vt_entry = max(vTMin, t);
                vec3 vPos = ro + rd_norm * vt_entry;
                
                //Brick currentBrick = brickLenta[brickIdx];

                //uint localVoxels[128];
                //for(int j = 0; j < 128; j++) {
                //    localVoxels[j] = brickLenta[brickIdx].voxels[j];
                //}

                // ВАЖНО: Смещение вперед по направлению луча, чтобы гарантированно зайти внутрь вокселя
                vPos += rd_norm * 0.0005;  

                ivec3 voxelPos = ivec3(floor(vPos));
                voxelPos = clamp(voxelPos, brickPos * 8, brickPos * 8 + ivec3(7));

                vec3 vDeltaT = abs(1.0 / safeRd);
                
                vec3 vSideT;
                vSideT.x = (step.x > 0) ? (float(voxelPos.x + 1) - vPos.x) * vDeltaT.x : (vPos.x - float(voxelPos.x)) * vDeltaT.x;
                vSideT.y = (step.y > 0) ? (float(voxelPos.y + 1) - vPos.y) * vDeltaT.y : (vPos.y - float(voxelPos.y)) * vDeltaT.y;
                vSideT.z = (step.z > 0) ? (float(voxelPos.z + 1) - vPos.z) * vDeltaT.z : (vPos.z - float(voxelPos.z)) * vDeltaT.z;
                vSideT += vt_entry; // Привязываем к текущему таймеру входа

                ivec3 vMask = ivec3(0);
                int microSteps = 0;
                float currentVt = vt_entry;
                
                // --- МИКРО-ЦИКЛ ШАГА ПО ВОКСЕЛЯМ ---
                while (microSteps++ < MICRO_STEPS) {
                    if (currentVt >= vTMax || currentVt >= localClosestT) break;
                    if ((voxelPos >> 3) != brickPos) break; // Вылетели из брика
                    
                    int vx = voxelPos.x & 7;
                    int vy = voxelPos.y & 7;
                    int vz = voxelPos.z & 7;
                    
                    uint voxelID = getVoxelID(brickIdx, vx, vy, vz);

                    //uint linearIdx = uint(vx | (vy << 3) | (vz << 6));
                    //uint uintIdx = linearIdx >> 2u;
                    //uint byteOffset = (linearIdx & 3u) * 8u;
                    //uint voxelID = (currentBrick.voxels[uintIdx] >> byteOffset) & 0xFFu;
                    
                    //uint voxelID = (localVoxels[uintIdx] >> byteOffset) & 0xFFu;

                    if (voxelID != 0u) {
                        localClosestT = currentVt;
                        hitMaterialID = matOffset + voxelID;
                        mask = vMask;
                        hitFound = true;
                        break;
                    }
                    
                    // Шаг микро-DDA
                    if (vSideT.x < vSideT.y) {
                        if (vSideT.x < vSideT.z) { currentVt = vSideT.x; vSideT.x += vDeltaT.x; voxelPos.x += step.x; vMask = ivec3(1, 0, 0); }
                        else                     { currentVt = vSideT.z; vSideT.z += vDeltaT.z; voxelPos.z += step.z; vMask = ivec3(0, 0, 1); }
                    } else {
                        if (vSideT.y < vSideT.z) { currentVt = vSideT.y; vSideT.y += vDeltaT.y; voxelPos.y += step.y; vMask = ivec3(0, 1, 0); }
                        else                     { currentVt = vSideT.z; vSideT.z += vDeltaT.z; voxelPos.z += step.z; vMask = ivec3(0, 0, 1); }
                    }
                }
                
                if (hitFound) break;

                t = vTMax;

                // Пересчитываем макро-параметры sideT для следующего шага, основываясь на новом t
                vec3 exitPos = ro + rd_norm * t;
                sideT.x = (step.x > 0) ? (float(brickPos.x + 1) * 8.0 - exitPos.x) * (deltaT.x / 8.0) : (exitPos.x - float(brickPos.x) * 8.0) * (deltaT.x / 8.0);
                sideT.y = (step.y > 0) ? (float(brickPos.y + 1) * 8.0 - exitPos.y) * (deltaT.y / 8.0) : (exitPos.y - float(brickPos.y) * 8.0) * (deltaT.y / 8.0);
                sideT.z = (step.z > 0) ? (float(brickPos.z + 1) * 8.0 - exitPos.z) * (deltaT.z / 8.0) : (exitPos.z - float(brickPos.z) * 8.0) * (deltaT.z / 8.0);
                sideT += t;
            }
        }
        
        // Шаг макро-DDA на следующий брик
        if (sideT.x < sideT.y) {
            if (sideT.x < sideT.z) { t = sideT.x; sideT.x += deltaT.x; brickPos.x += step.x; }
            else                     { t = sideT.z; sideT.z += deltaT.z; brickPos.z += step.z; }
        } else {
            if (sideT.y < sideT.z) { t = sideT.y; sideT.y += deltaT.y; brickPos.y += step.y; }
            else                     { t = sideT.z; sideT.z += deltaT.z; brickPos.z += step.z; }
        }
    }

    if (hitFound) {
        closestT = localClosestT * VOXEL_SIZE ;
        normal = -vec3(step) * vec3(mask);
        return true;
    }

    return false;
}

bool traceBVH(uint rootNode, vec3 ro, vec3 rd, inout float closestT, out uint matID, out vec3 normal){
    matID = 0;
    normal = vec3(0);
    bool hitFound = false;
    float minHitT = closestT;

    uint stack[64];
    int stackPtr = 0;
    stack[0] = rootNode;

    while (stackPtr >= 0) {
        uint currentNode = stack[stackPtr--];
        BVHNode node = bvhNodes[currentNode];

        float tMin, tMax;
        if (!intersectAABB(ro, rd, node.boxMin.xyz, node.boxMax.xyz, tMin, tMax))
            continue;

        if (tMin > minHitT) continue;

        int objectIndex = int(node.metaData.x);

        if (objectIndex != -1) {
            // Это листовой узел - проверяем пересечение с объектом
            GpuEntityMeta objMeta = entities[objectIndex];
            
            float tObj = minHitT;
            uint tempMatID = 0;
            vec3 tempNormal = vec3(0);
            
            // Вызываем traceVoxelObject для проверки пересечения
            if (traceVoxelObject(objMeta, ro, rd, tObj, tempMatID, tempNormal)) {
                 
                minHitT = tObj;
                matID = tempMatID;
                normal = tempNormal;
                hitFound = true;
                
            }
        } else {
            // Внутренний узел - добавляем дочерние узлы
            int leftChild = int(node.boxMin.w);
            int rightChild = int(node.boxMax.w);
            
            // Оптимизация: сначала добавляем ближайший узел
            if (leftChild != -1 && rightChild != -1) {
                float leftTMin, leftTMax, rightTMin, rightTMax;
                bool leftHit = intersectAABB(ro, rd, bvhNodes[leftChild].boxMin.xyz, 
                                            bvhNodes[leftChild].boxMax.xyz, leftTMin, leftTMax);
                bool rightHit = intersectAABB(ro, rd, bvhNodes[rightChild].boxMin.xyz, 
                                             bvhNodes[rightChild].boxMax.xyz, rightTMin, rightTMax);
                
                if (leftHit && rightHit) {
                    // Добавляем дальний узел первым, ближний - последним (LIFO)
                    if (leftTMin < rightTMin) {
                        stack[++stackPtr] = uint(rightChild);
                        stack[++stackPtr] = uint(leftChild);
                    } else {
                        stack[++stackPtr] = uint(leftChild);
                        stack[++stackPtr] = uint(rightChild);
                    }
                } else if (leftHit) {
                    stack[++stackPtr] = uint(leftChild);
                } else if (rightHit) {
                    stack[++stackPtr] = uint(rightChild);
                }
            } else if (leftChild != -1) {
                stack[++stackPtr] = uint(leftChild);
            } else if (rightChild != -1) {
                stack[++stackPtr] = uint(rightChild);
            }
            
            // Защита от переполнения стека
            if (stackPtr > 60) break;
        }
    }

    if (hitFound) {
        closestT = minHitT;
        return true;
    }
    return false;
}

bool traceChunkGrid(vec3 ro, vec3 rd, out uint hitMaterialID, out vec3 normal, inout float finalT){
    hitMaterialID = 0u;
    normal = vec3(0.0);
    bool hitAny = false;

    // Инициализация координат и шагов макро-DDA по чанкам мира
    ivec3 chunkPos = ivec3(floor(ro / CHUNK_SIZE_METERS));
    ivec3 step = ivec3(rd.x >= 0.0 ? 1 : -1, rd.y >= 0.0 ? 1 : -1, rd.z >= 0.0 ? 1 : -1);
    ivec3 cameraChunkPos = ivec3(floor(cameraPos / CHUNK_SIZE_METERS));

    vec3 deltaT = getChunkDeltaT(rd) * CHUNK_SIZE_METERS;
    vec3 sideT;
    sideT.x = (step.x > 0) ? (float(chunkPos.x + 1) * CHUNK_SIZE_METERS - ro.x) * (deltaT.x / CHUNK_SIZE_METERS) : (ro.x - float(chunkPos.x) * CHUNK_SIZE_METERS) * (deltaT.x / CHUNK_SIZE_METERS);
    sideT.y = (step.y > 0) ? (float(chunkPos.y + 1) * CHUNK_SIZE_METERS - ro.y) * (deltaT.y / CHUNK_SIZE_METERS) : (ro.y - float(chunkPos.y) * CHUNK_SIZE_METERS) * (deltaT.y / CHUNK_SIZE_METERS);
    sideT.z = (step.z > 0) ? (float(chunkPos.z + 1) * CHUNK_SIZE_METERS - ro.z) * (deltaT.z / CHUNK_SIZE_METERS) : (ro.z - float(chunkPos.z) * CHUNK_SIZE_METERS) * (deltaT.z / CHUNK_SIZE_METERS);
    
    float t = 0.0;

    for (int i = 0; i < 32; i++) {
        // ОПТИМИЗАЦИЯ: Если текущий шаг луча по сетке чанков улетел ДАЛЬШЕ, 
        // чем любое уже найденное пересечение (например, воксель танка в предыдущем чанке)
        if (t >= finalT) {
            break; 
        }

        // Проверка выхода за пределы активной зоны видимости вокруг камеры
        ivec3 distToCam = abs(chunkPos - cameraChunkPos);
        if (distToCam.x >= TEXTURE_SIZE / 2 || distToCam.y >= TEXTURE_SIZE / 2 || distToCam.z >= TEXTURE_SIZE / 2) {
            break;
        }

        
        // Кольцевое зацикливание координат текстуры (Твой оригинальный mod)
        ivec3 texCoords = ivec3(
            int(mod(float(chunkPos.x), float(TEXTURE_SIZE))),
            int(mod(float(chunkPos.y), float(TEXTURE_SIZE))),
            int(mod(float(chunkPos.z), float(TEXTURE_SIZE)))
        );

        // Одновременная выборка: ID ландшафта и ID корня BVH объектов для данной точки пространства
        uint chunkID = texelFetch(worldChunkMap, texCoords, 0).r;
        uint bvhNodeID = texelFetch(bvhMap, texCoords, 0).r;
        
        // -----------------------------------------------------------------
        // ПОД-ЭТАП А: ПРОВЕРЯЕМ ДИНАМИЧЕСКИЕ ОБЪЕКТЫ В ЭТОЙ ЯЧЕЙКЕ МИРА
        // -----------------------------------------------------------------
        // Проверяем, есть ли объекты в этой точке (даже если ландшафт тут равен 0, т.е. воздух)
        if (bvhNodeID != 0u) {
            float tObj = finalT;
            uint objMatID;
            vec3 objNormal;
            if(debugFlag) return true;
            // Запускаем трассировку BVH объектов для этого региона
            if (traceBVH(bvhNodeID, ro, rd, tObj, objMatID, objNormal)) {
                if (tObj < finalT) {
                    finalT = tObj;
                    hitMaterialID = objMatID;
                    normal = objNormal;
                    hitAny = true;
                    // ВАЖНО: Мы НЕ делаем здесь return или break!
                    // Луч запомнил хит в объекте, но продолжает лететь дальше по чанку, 
                    // чтобы проверить, нет ли ландшафта (земли) ПЕРЕД этим объектом.
                }
            }
        }

        // -----------------------------------------------------------------
        // ПОД-ЭТАП Б: ПРОВЕРЯЕМ СТАТИЧЕСКИЙ ЛАНДШАФТ ЧАНКА
        // -----------------------------------------------------------------
        if (chunkID != 0) {
            ChunkMeta eMeta = chunks[chunkID];
            float tLand = finalT;
            uint tempMatID;
            vec3 tempNormal;

            // Твой оригинальный метод прохода луча по вокселям ландшафта чанка
            if (traceChunkObject(eMeta, ro, rd, tLand, tempMatID, tempNormal)) {
                if (tLand < finalT) {
                    hitMaterialID = tempMatID;
                    normal = tempNormal;
                    finalT = tLand;
                    hitAny = true;
                    
                    // Так как ландшафт абсолютно непрозрачен, и он оказался ближе всего в этой точке,
                    // за ним ничего быть не может. СТОП МАШИНА! Выходим из всего трейсера.
                    return true; 
                }
            }
        }

        // Продвижение макро-DDA к следующей ячейке чанка мира (Твой оригинальный код шага)
        if (sideT.x < sideT.y) {
            if (sideT.x < sideT.z) {
                t = sideT.x; sideT.x += deltaT.x; chunkPos.x += step.x;
            } else {
                t = sideT.z; sideT.z += deltaT.z; chunkPos.z += step.z;
            }
        } else {
            if (sideT.y < sideT.z) {
                t = sideT.y; sideT.y += deltaT.y; chunkPos.y += step.y;
            } else {
                t = sideT.z; sideT.z += deltaT.z; chunkPos.z += step.z;
            }
        }
    }

    return hitAny;
}

void main() {
    ivec2 pixelCoords = ivec2(gl_GlobalInvocationID.xy);
    ivec2 texSize = imageSize(imgOutput);
    
    if (pixelCoords.x >= texSize.x || pixelCoords.y >= texSize.y) return;
    
    vec2 res = vec2(texSize);
    vec2 uv = (vec2(pixelCoords) + 0.5f - 0.5f * res) / res.y;
    
    vec3 currentRayDir = normalize(vec3(uv / 0.5f, 1.0f));
    currentRayDir = normalize((inverse(viewMatrix) * vec4(currentRayDir, 0.0f)).xyz);
    vec3 currentRayOrigin = cameraPos;
    
    vec3 accumulatedColor = vec3(0.0);
    vec3 throughput = vec3(1.0);
    
    int maxBounces = 1;
    
    for(int bounce = 0; bounce < maxBounces; bounce++) {
        
        float closestT = 1e9;
        uint materialID = 0u;
        vec3 normal = vec3(0.0);
        bool hitAny = false;

        hitAny = traceChunkGrid(currentRayOrigin,currentRayDir,materialID, normal,closestT);

        if (!hitAny) {
            float skyGradient = max(0.0, currentRayDir.y * 0.5 + 0.5);
            vec3 skyColor = mix(vec3(0.05, 0.1, 0.2), vec3(0.4, 0.6, 0.9), skyGradient);
            accumulatedColor += skyColor * throughput;
            break;
        } 
        else {
            vec3 voxelColor;
            float fuzz;
            float emission;
            UnpackMaterial(materialID, voxelColor, fuzz, emission);
            
            if (length(normal) < 0.1) normal = vec3(0.0, 1.0, 0.0);

            vec3 lightDirN = normalize(lightDir);
            float NdotL = max(dot(normal, lightDirN), 0.0);
            vec3 diffuse = voxelColor * NdotL * 0.7;
            vec3 ambient = voxelColor * 0.15;
            float rim = pow(1.0 - abs(dot(normal, currentRayDir)), 3.0) * 0.3;
            vec3 litColor = ambient + diffuse + voxelColor * rim;
            if (emission > 0.0) litColor += voxelColor * emission * 2.0;

            accumulatedColor += litColor * throughput * fuzz;

            vec3 hitPos = currentRayOrigin + currentRayDir * closestT;
            currentRayOrigin = hitPos + normal * 0.005;
            currentRayDir = normalize(reflect(currentRayDir, normal));
            throughput *= voxelColor * (1.0 - fuzz);

            if (length(throughput) < 0.01) break;
        }
    }
    
    
    accumulatedColor = pow(accumulatedColor, vec3(1.0 / 2.2));
    

    imageStore(imgOutput, pixelCoords, vec4(accumulatedColor, 1.0));
}