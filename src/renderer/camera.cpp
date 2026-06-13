#include "Camera.hpp"
#include <iostream>

Camera::Camera()
{
    position = glm::vec3(0.0f, 0.0f, 3.0f);
    rotation = glm::vec3(0.0f);

    view = glm::mat4(1.0f);
    projection = glm::mat4(1.0f);
}

void Camera::Update(GLFWwindow* window, float dt)
{
    if (glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS)
    {
        cursorLocked = true;
        firstMouse = true;
    }

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        cursorLocked = false;

    glfwSetInputMode(window,
        GLFW_CURSOR,
        cursorLocked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);

    if (cursorLocked)
    {
        ProcessMouse(window);
        ProcessInput(window, dt);
    }

    UpdateView();
}

void Camera::ProcessMouse(GLFWwindow* window)
{
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);

    if (firstMouse)
    {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    double xoffset = xpos - lastX;
    double yoffset = ypos - lastY;

    lastX = xpos;
    lastY = ypos;

    xoffset *= sensitivity;
    yoffset *= sensitivity;

    rotation.y += (float)xoffset;
    rotation.x += (float)yoffset;

    if (rotation.x > 89.0f) rotation.x = 89.0f;
    if (rotation.x < -89.0f) rotation.x = -89.0f;
}

void Camera::ProcessInput(GLFWwindow* window, float dt)
{
    float velocity = speed * dt;

    // Считаем полное направление взгляда
    float yaw = glm::radians(rotation.y);
    float pitch = glm::radians(rotation.x);

    forward.x = cos(pitch) * sin(yaw);
    forward.y = sin(pitch);
    forward.z = cos(pitch) * cos(yaw);
    forward = glm::normalize(forward);

    // Вектор движения по земле (без учета наклона головы Y)
    glm::vec3 walkForward = glm::normalize(glm::vec3(forward.x, 0.0f, forward.z));
    glm::vec3 right = glm::normalize(glm::cross(walkForward, glm::vec3(0, 1, 0)));
    glm::vec3 up = glm::vec3(0, 1, 0);

    // ИСПРАВЛЕНО: W — летим ВПЕРЕД (плюс), S — НАЗАД (минус)
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        position -= walkForward * velocity;

    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        position += walkForward * velocity;

    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        position -= right * velocity;

    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        position += right * velocity;

    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
        position += up * velocity;

    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        position -= up * velocity;
}

void Camera::UpdateView()
{
    float yaw = glm::radians(rotation.y);
    float pitch = glm::radians(rotation.x);

    glm::vec3 forward;
    forward.x = cos(pitch) * sin(yaw);
    forward.y = sin(pitch);
    forward.z = cos(pitch) * cos(yaw);
    forward = glm::normalize(forward);

    // Матрица взгляда строится строго вперед от позиции
    view = glm::lookAt(position, position + forward, glm::vec3(0, 1, 0));
}

void Camera::SetPosition(const glm::vec3& pos)
{
    position = pos;
}

void Camera::SetRotation(const glm::vec3& rot)
{
    rotation = rot;
}

const glm::mat4& Camera::GetViewMatrix() const
{
    return view;
}

const glm::mat4& Camera::GetProjectionMatrix() const
{
    return projection;
}

glm::vec3 Camera::GetPosition() const
{
    return position;
}

void Camera::SetPerspective(float fov, float aspect, float nearP, float farP)
{
    projection = glm::perspective(glm::radians(fov), aspect, nearP, farP);
}

void Camera::SetOrthographic(float left, float right, float bottom, float top, float nearP, float farP)
{
    projection = glm::ortho(left, right, bottom, top, nearP, farP);
}