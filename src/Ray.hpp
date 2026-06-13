#pragma once

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

struct Ray {
    glm::vec3 origin;
    glm::vec3 direction;
};

// Функция генерирует луч точно так же, как это делается в Compute/Fragment шейдере рейтрейсинга
Ray GetRaytracingMouseRay(GLFWwindow* window,
    glm::vec3 camPos,
    glm::vec3 camForward,
    glm::vec3 camUp,
    float fovDegrees)
{
    camForward *= -1;
    // 1. Получаем размеры окна и позицию мыши
    int width, height;
    glfwGetWindowSize(window, &width, &height);

    double mouseX, mouseY;
    glfwGetCursorPos(window, &mouseX, &mouseY);

    if (width <= 0 || height <= 0) {
        return Ray{ camPos, camForward };
    }

    // 2. Переводим координаты мыши в NDC (от -1 до 1)
    // Разворачиваем Y, так как в GLFW ноль вверху, а в рейтрейсинге — внизу экрана
    float ndcX = 1.0f - (2.0f * static_cast<float>(mouseX)) / static_cast<float>(width) ;
    float ndcY = 1.0f - (2.0f * static_cast<float>(mouseY)) / static_cast<float>(height);

    // 3. Вычисляем геометрию кадра на основе FOV и Aspect Ratio
    float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
    float fovRadians = glm::radians(fovDegrees);
    float halfTanFov = glm::tan(fovRadians * 0.5f);

    // 4. Строим базис камеры (ортонормированный вектор «вправо»)
    glm::vec3 camRight = glm::normalize(glm::cross(camForward, camUp));
    // Пересчитываем точный Up, чтобы исключить неортогональность
    glm::vec3 trueUp = glm::cross(camRight, camForward);

    // 5. Вычисляем направление луча в мировых координатах
    // Эта формула — точная копия того, что обычно написано в вашем рейтрейсинг-шейдере:
    // rayDir = forward + right * ndcX * scaleX + up * ndcY * scaleY
    glm::vec3 rayDir = camForward +
        camRight * (ndcX * halfTanFov * aspectRatio) +
        trueUp * (ndcY * halfTanFov);

    Ray ray;
    ray.origin = camPos;
    ray.direction = glm::normalize(rayDir);

    return ray;
}
