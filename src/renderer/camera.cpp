#include "Camera.hpp"
#include <iostream>

Camera::Camera()
{
    position = glm::vec3(0.0f, 5.0f, 0.0f);
    rotation = glm::vec3(0.0f);

    view = glm::mat4(1.0f);
    projection = glm::mat4(1.0f);
}

// В Camera.cpp:
void Camera::Update(const glm::vec3& playerEyePos, const glm::vec3& playerRotation)
{
    position = playerEyePos;
    rotation = playerRotation;

    float yaw = glm::radians(rotation.y);
    float pitch = glm::radians(rotation.x);

    // Просто собираем направление взгляда для lookAt
    
    forward.x = cos(pitch) * sin(yaw);
    forward.y = sin(pitch);
    forward.z = cos(pitch) * cos(yaw);
    forward = glm::normalize(forward);

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