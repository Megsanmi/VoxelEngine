#pragma once

inline bool intersectRayAABB(glm::vec3 ro, glm::vec3 rd, glm::vec3 bmin, glm::vec3 bmax, float& tMin, float& tMax) {
    glm::vec3 invRd = 1.0f / (rd + glm::vec3(1e-8f));
    glm::vec3 t0 = (bmin - ro) * invRd;
    glm::vec3 t1 = (bmax - ro) * invRd;
    glm::vec3 tmin3 = glm::min(t0, t1);
    glm::vec3 tmax3 = glm::max(t0, t1);
    tMin = glm::max(glm::max(tmin3.x, tmin3.y), tmin3.z);
    tMax = glm::min(glm::min(tmax3.x, tmax3.y), tmax3.z);
    return tMin <= tMax && tMax > 0.0f;
}
