#version 430 core

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

int MICRO_STEPS = 24;
int STEPS = 100;

// ==========================================
// СТРУКТУРЫ ДАННЫХ И БУФЕРЫ (Полное соответствие C++)
// ==========================================

struct GpuEntityMeta {
    mat4 invModelMatrix;
    mat4 modelMatrix;
    vec4 boxMin;        // w не используется
    vec4 boxMax;        // w не используется

    ivec4 sizeInBricks; // x, y, z - размеры, w - macroOffset в ленте макро-сеток
    ivec4 sizeInVoxels; // x, y, z - размеры, w - matOffset в ленте материалов
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


layout(rgba8, binding = 0) uniform writeonly image2D imgOutput;

// ==========================================
// ЮНИФОРМЫ КАМЕРЫ И СЦЕНЫ
// ==========================================
uniform mat4 viewMatrix;
uniform vec3 cameraPos;
uniform vec3 lightDir = vec3(0.5, 0.6, 0.7);
uniform float time;
uniform int NumEntities = 2; // Передаем общее количество объектов: objects.size()

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

    // 1. Трансформируем луч в локальное пространство
    vec3 ro = vec3(meta.invModelMatrix * vec4(worldRo, 1.0));
    vec3 rd = vec3(meta.invModelMatrix * vec4(worldRd, 0.0)); 
    
    float rayScale = length(rd);
    vec3 rd_norm = rd / rayScale;

    float tMin, tMax;
    vec3 maxBounds = vec4(meta.sizeInVoxels).xyz;
    
    if (!intersectAABB(ro, rd_norm, vec3(0.0), maxBounds, tMin, tMax)) return false;

    vec3 worldLimitPoint = worldRo + worldRd * closestT;
    vec3 localLimitPoint = vec3(meta.invModelMatrix * vec4(worldLimitPoint, 1.0));
    float localClosestT = dot(localLimitPoint - ro, rd_norm);
    
    float t = max(tMin, 0.0);
    if (t >= localClosestT) return false;
    
    vec3 pos = ro + rd_norm * t;
    pos += rd_norm * 0.001;

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
    vec3 deltaT = abs(1.0 / safeRd) * 8.0; 

    ivec3 brickPos = ivec3(floor(pos / 8.0));
    brickPos = clamp(brickPos, ivec3(0), brickSize - ivec3(1));

    vec3 sideT;
    sideT.x = (step.x > 0) ? (float(brickPos.x + 1) * 8.0 - pos.x) * (deltaT.x / 8.0) : (pos.x - float(brickPos.x) * 8.0) * (deltaT.x / 8.0);
    sideT.y = (step.y > 0) ? (float(brickPos.y + 1) * 8.0 - pos.y) * (deltaT.y / 8.0) : (pos.y - float(brickPos.y) * 8.0) * (deltaT.y / 8.0);
    sideT.z = (step.z > 0) ? (float(brickPos.z + 1) * 8.0 - pos.z) * (deltaT.z / 8.0) : (pos.z - float(brickPos.z) * 8.0) * (deltaT.z / 8.0);

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
                vPos += vec3(step) * 0.0005;  

                ivec3 voxelPos = ivec3(floor(vPos));
               
                if (voxelPos.x < 0 || voxelPos.x >= meta.sizeInVoxels.x ||
                    voxelPos.y < 0 || voxelPos.y >= meta.sizeInVoxels.y ||
                    voxelPos.z < 0 || voxelPos.z >= meta.sizeInVoxels.z) {
                    
                    if (sideT.x < sideT.y) {
                        if (sideT.x < sideT.z) { sideT.x += deltaT.x; brickPos.x += step.x; t = sideT.x - deltaT.x; }
                        else { sideT.z += deltaT.z; brickPos.z += step.z; t = sideT.z - deltaT.z; }
                    } else {
                        if (sideT.y < sideT.z) { sideT.y += deltaT.y; brickPos.y += step.y; t = sideT.y - deltaT.y; }
                        else { sideT.z += deltaT.z; brickPos.z += step.z; t = sideT.z - deltaT.z; }
                    }
                    continue; 
                }

                vec3 vDeltaT = abs(1.0 / safeRd);
                
                vec3 vSideT;
                vSideT.x = (step.x > 0) ? (float(voxelPos.x + 1) - vPos.x) * vDeltaT.x : (vPos.x - float(voxelPos.x)) * vDeltaT.x;
                vSideT.y = (step.y > 0) ? (float(voxelPos.y + 1) - vPos.y) * vDeltaT.y : (vPos.y - float(voxelPos.y)) * vDeltaT.y;
                vSideT.z = (step.z > 0) ? (float(voxelPos.z + 1) - vPos.z) * vDeltaT.z : (vPos.z - float(voxelPos.z)) * vDeltaT.z;
                
                ivec3 vMask = ivec3(0);

                vec3 distToNext = vec3(
                    vSideT.x / vDeltaT.x,
                    vSideT.y / vDeltaT.y,
                    vSideT.z / vDeltaT.z
                );
                
                if (distToNext.x > distToNext.y && distToNext.x > distToNext.z) {
                    vMask = ivec3(1, 0, 0);
                } else if (distToNext.y > distToNext.z) {
                    vMask = ivec3(0, 1, 0);
                } else {
                    vMask = ivec3(0, 0, 1);
                }

                int microSteps = 0;
                float currentVt = vt_entry;
                
                while (microSteps++ < MICRO_STEPS) {
                    if (currentVt > vTMax + 0.0001 || currentVt >= localClosestT) break;
                    
                    if (voxelPos.x < 0 || voxelPos.x >= meta.sizeInVoxels.x ||
                        voxelPos.y < 0 || voxelPos.y >= meta.sizeInVoxels.y ||
                        voxelPos.z < 0 || voxelPos.z >= meta.sizeInVoxels.z) break;
                    
                    if ((voxelPos >> 3) != brickPos) break;
                    
                    int vx = voxelPos.x & 7;
                    int vy = voxelPos.y & 7;
                    int vz = voxelPos.z & 7;
                    
                    uint voxelID = getVoxelID(brickIdx, vx, vy, vz);
                    if (voxelID != 0u) {
                        localClosestT = currentVt;
                        hitMaterialID = matOffset + voxelID;
                        mask = vMask;
                        hitFound = true;
                        break;
                    }
                    
                    // Шаг: currentVt = минимальное vSideT (до увеличения)
                    if (vSideT.x < vSideT.y) {
                        if (vSideT.x < vSideT.z) {
                            currentVt = vSideT.x;
                            vSideT.x += vDeltaT.x;
                            voxelPos.x += step.x;
                            vMask = ivec3(1, 0, 0);
                        } else {
                            currentVt = vSideT.z;
                            vSideT.z += vDeltaT.z;
                            voxelPos.z += step.z;
                            vMask = ivec3(0, 0, 1);
                        }
                    } else {
                        if (vSideT.y < vSideT.z) {
                            currentVt = vSideT.y;
                            vSideT.y += vDeltaT.y;
                            voxelPos.y += step.y;
                            vMask = ivec3(0, 1, 0);
                        } else {
                            currentVt = vSideT.z;
                            vSideT.z += vDeltaT.z;
                            voxelPos.z += step.z;
                            vMask = ivec3(0, 0, 1);
                        }
                    }
                }
                if (hitFound) break;
            }
        }
        
        // Шаг макро-DDA
        if (sideT.x < sideT.y) {
            if (sideT.x < sideT.z) { 
                t = sideT.x;
                sideT.x += deltaT.x; 
                brickPos.x += step.x; 
            }
            else { 
                t = sideT.z;
                sideT.z += deltaT.z; 
                brickPos.z += step.z; 
            }
        } else {
            if (sideT.y < sideT.z) { 
                t = sideT.y;
                sideT.y += deltaT.y; 
                brickPos.y += step.y; 
            }
            else { 
                t = sideT.z;
                sideT.z += deltaT.z; 
                brickPos.z += step.z; 
            }
        }
    }
    if (hitFound) {
        vec3 localHitPoint = ro + rd_norm * localClosestT;
        vec3 worldHitPoint = vec3(meta.modelMatrix * vec4(localHitPoint, 1.0));
        closestT = dot(worldHitPoint - worldRo, worldRd);
        
        vec3 localNormal = vec3(mask) * vec3(-step);
        normal = normalize(mat3(meta.modelMatrix) * localNormal);
        return true;
    }
    
    return false;
}

float random(vec2 st) {
    return fract(sin(dot(st.xy, vec2(12.9898, 78.233))) * 43758.5453123);
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
    
    // Для отрисовки рёбер BVH
    float bestEdgeT = 1e9;
    vec3 bestEdgeColor = vec3(0.0);
    bool showBVH = true; // Переключатель
    
    int maxBounces = 1;
    
    for(int bounce = 0; bounce < maxBounces; bounce++) {
        
        float closestT = 1e9;
        uint materialID = 0u;
        vec3 normal = vec3(0.0);
        bool hitAny = false;

        int stack[64];
        int stackPtr = 0;
        stack[0] = 0;

        while (stackPtr >= 0)
        {
            int nodeIdx = stack[stackPtr--];
            BVHNode node = bvhNodes[nodeIdx];

            float tMin, tMax;
            if (tMin > closestT) continue;

            if (node.metaData.x >= 0)
            {
                int objIndex = node.metaData.x;

                uint matID;
                vec3 nrm;

                if (traceVoxelObject(entities[objIndex],
                                     currentRayOrigin,
                                     currentRayDir,
                                     closestT,
                                     matID,
                                     nrm))
                {
                    materialID = matID;
                    normal = nrm;
                    hitAny = true;
                }
                continue;
            }

            int left  = int(node.boxMin.w);
            int right = int(node.boxMax.w);

            float tMinL, tMaxL;
            float tMinR, tMaxR;

            bool hitL = intersectAABB(currentRayOrigin, currentRayDir,
                                      bvhNodes[left].boxMin.xyz,
                                      bvhNodes[left].boxMax.xyz,
                                      tMinL, tMaxL);

            bool hitR = intersectAABB(currentRayOrigin, currentRayDir,
                                      bvhNodes[right].boxMin.xyz,
                                      bvhNodes[right].boxMax.xyz,
                                      tMinR, tMaxR);

            if (hitL && tMinL > closestT) hitL = false;
            if (hitR && tMinR > closestT) hitR = false;

            if (hitL && hitR)
            {
                if (tMinL < tMinR)
                {
                    stack[++stackPtr] = right;
                    stack[++stackPtr] = left;
                }
                else
                {
                    stack[++stackPtr] = left;
                    stack[++stackPtr] = right;
                }
            }
            else if (hitL)
                stack[++stackPtr] = left;
            else if (hitR)
                stack[++stackPtr] = right;
        }

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

            float fogFactor = 1.0 - exp(-closestT * 0.0005);
            litColor = mix(litColor, vec3(0.6, 0.7, 0.9), fogFactor);
            
            

            accumulatedColor += litColor * throughput * fuzz;

            vec3 hitPos = currentRayOrigin + currentRayDir * closestT;
            currentRayOrigin = hitPos + normal * 0.005;
            currentRayDir = normalize(reflect(currentRayDir, normal));
            throughput *= voxelColor * (1.0 - fuzz);

            if (length(throughput) < 0.01) break;
        }
    }
    
    // Постпроцессинг
    vec2 screenUV = vec2(pixelCoords) / vec2(texSize);
    float vignette = 1.0 - dot(screenUV - 0.5, screenUV - 0.5) * 0.5;
    accumulatedColor *= vignette;
    accumulatedColor = pow(accumulatedColor, vec3(1.0 / 2.2));
    float dither = fract(sin(dot(screenUV * texSize, vec2(12.9898, 78.233))) * 43758.5453);
    accumulatedColor += (dither - 0.5) / 255.0;
    
    imageStore(imgOutput, pixelCoords, vec4(accumulatedColor, 1.0));
}