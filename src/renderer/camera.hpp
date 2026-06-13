#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <GLFW/glfw3.h>

class Camera
{
public:
    Camera();

    void Update(GLFWwindow* window, float dt);

    void SetPosition(const glm::vec3& pos);
    void SetRotation(const glm::vec3& rot);

    const glm::mat4& GetViewMatrix() const;
    const glm::mat4& GetProjectionMatrix() const;
    glm::vec3 GetPosition() const;

    void SetPerspective(float fov, float aspect, float nearP, float farP);
    void SetOrthographic(float left, float right, float bottom, float top, float nearP, float farP);
    glm::vec3 getForward() { return forward; };


    void UpdateView();

    void ProcessInput(GLFWwindow* window, float dt);
    void ProcessMouse(GLFWwindow* window);

private:
    glm::vec3 forward;
    glm::vec3 position;
    glm::vec3 rotation; 

    glm::mat4 view;
    glm::mat4 projection;

    float speed = 200.0f;
    float sensitivity = 0.1f;

    bool firstMouse = true;
    double lastX = 0.0;
    double lastY = 0.0;
    bool cursorLocked = true;
};