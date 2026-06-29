#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <iostream>
#include "renderer/ShaderProgram.hpp"
#include "renderer/camera.hpp"
#include "scene.hpp"
#include "UI.hpp"
#include "Ray.hpp"
#include "StaticGrid.hpp"



float screenVertices[] = {
    -1.0f, -1.0f,
     1.0f, -1.0f,
    -1.0f,  1.0f,
     1.0f,  1.0f,
};
int width = 1924;
int height = 1024;


int main()
{
    // 1. Инициализация GLFW
    if (!glfwInit())
        return -1;

    // 2. Создание окна
    GLFWwindow* window = glfwCreateWindow(width, height, "VoxelEngine", nullptr, nullptr);
    if (!window)
    {
        glfwTerminate();
        return -1;
    }

    // 3. Делаем контекст текущим
    glfwMakeContextCurrent(window);

    // Инициализируем GLAD !!!
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        glfwTerminate();
        return -1;
    }

    // Теперь функции OpenGL можно безопасно вызывать
    glEnable(GL_DEPTH_TEST);

    // Вертикальная синхронизация 
    glfwSwapInterval(0);

    // Создаем шейдер (теперь GLAD инициализирован, и glCreateShader сработает)
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

    // Создаем пустую текстуру под разрешение экрана
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    // Настройки фильтрации (для квада лучше всего GL_LINEAR или GL_NEAREST)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    
    
    VoxelScene scene;
    scene.Init();
      
    

    EditorUI ui;
    ui.Init(window);
    ui.scene = &scene;

    StaticGrid world(&scene.manager,2);


    scene.LoadVox("assets/castle.vox");
    scene.LoadVox("assets/castle.vox");

    //for(int x = 0;x<10;x++ )
    //  //  for(int y = 0;y<10;y++ )
    //        for(int z = 0;z<10;z++ )
    //            scene.manager.GetObject(scene.LoadVox("assets/castle.vox")).transform.position = glm::vec3(x,1,z)*5.f;

    float dt;
    double timer = 0;
  
    
    //world.LoadChunk(glm::ivec3(0));
        
    // 4. Главный цикл
    while (!glfwWindowShouldClose(window))
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        dt = glfwGetTime() - timer;
        timer = glfwGetTime();
        
        

        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
        {
            float t = glfwGetTime();
            scene.SplitObject(0);
            //std::cout <<"time of splitting " << glfwGetTime() -  t<< std::endl;
            // Ваши параметры камеры (мировые координаты)
            glm::vec3 cameraPosition = scene.camera.GetPosition();
            glm::vec3 cameraForward = scene.camera.getForward(); // Вектор направления взгляда
            glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f); // Или ваш локальный Up камеры
            float fov = 90.0f; // Угол обзора в градусах (например, 60 или 45)

            Ray ray = GetRaytracingMouseRay(window, cameraPosition, cameraForward, cameraUp, fov);
            
            
            scene.RemoveVoxelByRay(ray.origin, ray.direction);
        }
     
        world.UpdateChunks(scene.camera.position);

        

        
        scene.camera.Update(window, dt);
        scene.Update(dt);

        compShader.use();
        compShader.setFloat("time", (float)glfwGetTime());
        compShader.setVec2("uResolution", glm::vec2(width, height)); // Используем переменные
        compShader.setMatrix4("viewMatrix", scene.camera.GetViewMatrix());
        compShader.setVec3("cameraPos", scene.camera.GetPosition());
        compShader.setInt("NumEntities", scene.manager.MaxObjectsEverCreated());
        compShader.setInt("NumChunks", scene.manager.MaxChunksEverCreated());

        // Биндим текстуру экрана
        glBindImageTexture(0, screenTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

        // 2. ПРАВИЛЬНЫЙ ЗАПУСК: делим реальное разрешение на размер локальной группы (16)
        glDispatchCompute((width + 15) / 16, (height + 15) / 16, 1);

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
