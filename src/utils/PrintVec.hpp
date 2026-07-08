#include <iostream>
#include <string_view>

namespace Utils {

    // ==========================================
    // СВЕРХБЫСТРЫЙ ДЕБАЖНЫЙ ВЫВОД (PRINT)
    // ==========================================

    // Выводит вектор GLM. Пример: JoltGLM::Print("Player Pos", mPosition);
    inline void Print(std::string_view label, const glm::vec3& v) {
        std::cout << label << ": [X: " << v.x << ", Y: " << v.y << ", Z: " << v.z << "]\n";
    }

    // Выводит вектор Jolt. Пример: JoltGLM::Print("Jolt Velocity", character->GetLinearVelocity());
    inline void Print(std::string_view label, const JPH::Vec3& v) {
        std::cout << label << ": [X: " << v.GetX() << ", Y: " << v.GetY() << ", Z: " << v.GetZ() << "]\n";
    }

    // Выводит кватернион GLM
    inline void Print(std::string_view label, const glm::quat& q) {
        std::cout << label << ": [W: " << q.w << ", X: " << q.x << ", Y: " << q.y << ", Z: " << q.z << "]\n";
    }

    // Выводит кватернион Jolt
    inline void Print(std::string_view label, const JPH::Quat& q) {
        std::cout << label << ": [W: " << q.GetW() << ", X: " << q.GetX() << ", Y: " << q.GetY() << ", Z: " << q.GetZ() << "]\n";
    }
}
