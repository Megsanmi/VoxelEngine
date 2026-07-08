#pragma once

// Подключаем GLM
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Подключаем базовые математические типы Jolt
#include <Jolt/Jolt.h>
#include <Jolt/Math/Vec3.h>
#include <Jolt/Math/Quat.h>
#include <Jolt/Math/Mat44.h>

namespace Utils {

    // ==========================================
    // ВЕКТОРЫ: ИЗ GLM В JOLT
    // ==========================================

    inline JPH::Vec3 ToJolt(const glm::vec3& v) {
        return JPH::Vec3(v.x, v.y, v.z);
    }

    inline JPH::Vec4 ToJolt(const glm::vec4& v) {
        return JPH::Vec4(v.x, v.y, v.z, v.w);
    }

    // ==========================================
    // ВЕКТОРЫ: ИЗ JOLT В GLM
    // ==========================================

    inline glm::vec3 ToGLM(const JPH::Vec3& v) {
        return glm::vec3(v.GetX(), v.GetY(), v.GetZ());
    }

    inline glm::vec4 ToGLM(const JPH::Vec4& v) {
        return glm::vec4(v.GetX(), v.GetY(), v.GetZ(), v.GetW());
    }

    // ==========================================
    // КВАТЕРНИОНЫ (ПОВОРОТЫ)
    // ==========================================

    inline JPH::Quat ToJolt(const glm::quat& q) {
        return JPH::Quat(q.x, q.y, q.z, q.w);
    }

    inline glm::quat ToGLM(const JPH::Quat& q) {
        return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); // Внимание: у GLM порядок (W, X, Y, Z) конструктора
    }

    // ==========================================
    // МАТРИЦЫ (4x4)
    // ==========================================

    // Из GLM (Column-major) в Jolt (Column-major)
    inline JPH::Mat44 ToJolt(const glm::mat4& m) {
        return JPH::Mat44(
            JPH::Vec4(m[0].x, m[0].y, m[0].z, m[0].w),
            JPH::Vec4(m[1].x, m[1].y, m[1].z, m[1].w),
            JPH::Vec4(m[2].x, m[2].y, m[2].z, m[2].w),
            JPH::Vec4(m[3].x, m[3].y, m[3].z, m[3].w)
        );
    }

    // Из Jolt (Column-major) в GLM (Column-major)
    inline glm::mat4 ToGLM(const JPH::Mat44& m) {
        glm::mat4 result;
        // Jolt хранит столбцы как Vec4. Получаем их через GetColumn
        JPH::Vec4 c0 = m.GetColumn4(0);
        JPH::Vec4 c1 = m.GetColumn4(1);
        JPH::Vec4 c2 = m.GetColumn4(2);
        JPH::Vec4 c3 = m.GetColumn4(3);

        result[0] = glm::vec4(c0.GetX(), c0.GetY(), c0.GetZ(), c0.GetW());
        result[1] = glm::vec4(c1.GetX(), c1.GetY(), c1.GetZ(), c1.GetW());
        result[2] = glm::vec4(c2.GetX(), c2.GetY(), c2.GetZ(), c2.GetW());
        result[3] = glm::vec4(c3.GetX(), c3.GetY(), c3.GetZ(), c3.GetW());
        return result;
    }

}
