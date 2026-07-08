#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <iostream>
#include "renderer/ShaderProgram.hpp"
#include "renderer/camera.hpp"
#include "Scene/scene.hpp"
#include "UI/UI.hpp"
#include "Ray.hpp"
#include "StaticGrid.hpp"
#include "VoxelObject/VoxelModifier.hpp"
#include "Scene/PhysicsWorld.hpp"
#include "Player/Player.hpp"
#include "steam_api.h" 
#include "NetCode/NetWork.hpp"
#include "NetCode/CallBacks.hpp"



float screenVertices[] = {
    -1.0f, -1.0f,
     1.0f, -1.0f,
    -1.0f,  1.0f,
     1.0f,  1.0f,
};
int width = 1924;
int height = 1024;

float renderScale = 0.8;

int main()
{
   
    if (!glfwInit())
        return -1;

    GLFWwindow* window = glfwCreateWindow(width, height, "VoxelEngine", nullptr, nullptr);
    if (!window)
    {
        glfwTerminate();
        return -1;
    }

    if (!SteamAPI_Init()) {
        std::cerr << "failed: SteamAPI_Init() return false!" << std::endl;
    }
    
    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        glfwTerminate();
      
    }

    glEnable(GL_DEPTH_TEST);

    glfwSwapInterval(1);

    ShaderProgram shader("shaders/raymarchingV.glsl", "shaders/raymarchingF.glsl");
    ShaderProgram compShader("shaders/comp.glsl");

    if (!shader.isCompiled()) {
        std::cout << "Error compile Shader" << std::endl;
    }

    glViewport(0, 0, width, height);
    
    GLuint quadVAO, quadVBO;
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);

    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(screenVertices), screenVertices, GL_STATIC_DRAW);
    
    // Настраиваем атрибут позиции (location = 0 в вершинном шейдере)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    
    //Текстура экрана
    GLuint screenTexture;
    glGenTextures(1, &screenTexture);
    glBindTexture(GL_TEXTURE_2D, screenTexture);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (int)width * renderScale, (int)height * renderScale, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    
    
    VoxelScene scene;
    scene.Init();
      
    

    EditorUI ui;
    ui.Init(window);
    ui.scene = &scene;



    StaticGrid world(&scene.manager,2);
    


    float dt;
    double timer = 0;
  
    
    static bool rightButtonPressedLastFrame = false;
    scene.LoadVox("assets/teapot.vox", glm::vec3(0,20,0));



    Player player;
    player.character = scene.manager.physicsWorld.CreateCharacter(JPH::Vec3(10, 15, 10));
    player.controller = &scene.controller;

    NetworkManager network(NetworkType::ENet);
    
    network.SetLogCallback([&ui](const std::string& message) {
        ui.AddConsoleMessage(message);
        });
    network.Init();
    ui.network = &network;

    ui.AddConsoleMessage("[System] Welcome to VoxelEngine!");
    ui.AddConsoleMessage("[System] Press ~ to open console");


    // 4. Главный цикл
    while (!glfwWindowShouldClose(window))
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        dt = glfwGetTime() - timer;
        timer = glfwGetTime();
        
        world.UpdateChunks(scene.camera.position);
        scene.manager.physicsWorld.UpdateVirtualCharacter(player.character, player.velocity, dt);


        
        player.Update(dt, scene.controller.PollKeyboardAndMouse(window));

        scene.camera.Update(player.GetEyePosition(), player.GetRotation());
        scene.Update(dt);

        network.Update();


        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
        {        
            glm::vec3 cameraPosition = scene.camera.GetPosition();
            glm::vec3 cameraForward = scene.camera.getForward(); 
            glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f); 
            float fov = 90.0f;

            Ray ray = GetRaytracingMouseRay(window, cameraPosition, cameraForward, cameraUp, fov);
            
            
            std::vector<std::pair<float, uint32_t>> sortedObjectIds;

            for (uint32_t id : scene.objectIDs) {
                
                const VoxelObject& obj = scene.manager.GetObject(id);

                glm::vec3 objCenter = obj.transform.position;
                float dist = glm::length(objCenter - ray.origin);
                sortedObjectIds.push_back({ dist, id });
            }
            std::sort(sortedObjectIds.begin(), sortedObjectIds.end());

            glm::ivec3 hitVoxelPos;

            
            for (auto& [dist, id] : sortedObjectIds) {
                VoxelObject& obj = scene.manager.GetObject(id);

                if (VoxelModifier::RemoveVoxelByRay(obj.voxelMap, obj.GetFinalModelMatrix(),ray.origin, ray.direction, hitVoxelPos)) {
                    scene.SplitObject(id);
                    scene.manager.OnObjectVoxelsChanged(id, hitVoxelPos.x, hitVoxelPos.y, hitVoxelPos.z);
                }
            }
        }
        
        bool isPressedNow = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);
        glm::vec3 cameraPosition = scene.camera.position;
        glm::vec3 cameraForward = scene.camera.getForward();
        glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
        float fov = 90.0f;

        Ray ray = GetRaytracingMouseRay(window, cameraPosition, cameraForward, cameraUp, fov);

        // 1. НАЖАТИЕ: Клик произошел именно в этом кадре
        if (isPressedNow && !rightButtonPressedLastFrame)
        {
            scene.HandleMouseClick(ray.origin, ray.direction);
        }
        // 2. УДЕРЖАНИЕ И ПЕРЕМЕЩЕНИЕ: Кнопка зажата (и в этом, и в прошлом кадре)
        else if (isPressedNow && rightButtonPressedLastFrame)
        {
            scene.HandleMouseMove(ray.origin, ray.direction);
        }
        // 3. ОТПУСКАНИЕ: Кнопку только что отпустили
        else if (!isPressedNow && rightButtonPressedLastFrame)
        {
            scene.HandleMouseRelease();
        }

        rightButtonPressedLastFrame = isPressedNow;

        compShader.use();
        compShader.setFloat("time", (float)glfwGetTime());
        compShader.setVec2("uResolution", glm::vec2(width * renderScale, height * renderScale)); // Используем переменные
        compShader.setMatrix4("viewMatrix", scene.camera.GetViewMatrix());
        compShader.setVec3("cameraPos", scene.camera.GetPosition());
        compShader.setInt("NumEntities", scene.manager.MaxObjectsEverCreated());
        compShader.setInt("NumChunks", scene.manager.MaxChunksEverCreated());

        
        
        // Биндим текстуру экрана
        glBindImageTexture(0, screenTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

        // 2. ПРАВИЛЬНЫЙ ЗАПУСК: делим реальное разрешение на размер локальной группы (16)
        glDispatchCompute((width * renderScale + 15) / 16, (height * renderScale + 15) / 16, 1);

        // Обязательный барьер
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        shader.use();

        
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, screenTexture);
        
        //Рисуем квад
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(0);


        ui.BeginFrame();
        ui.Draw();
        ui.EndFrame();


        glfwPollEvents();
        glfwSwapBuffers(window);
    }

    // 5. Завершение
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
