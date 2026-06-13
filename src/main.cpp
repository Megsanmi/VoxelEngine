#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <iostream>
#include "renderer/ShaderProgram.hpp"
#include "renderer/camera.hpp"
#include "scene.hpp"
#include "UI.hpp"
#include "Ray.hpp"



float screenVertices[] = {
    -1.0f, -1.0f,
     1.0f, -1.0f,
    -1.0f,  1.0f,
     1.0f,  1.0f,
};


int main()
{
    // 1. Инициализация GLFW
    if (!glfwInit())
        return -1;

    // 2. Создание окна
    GLFWwindow* window = glfwCreateWindow(1924, 1024, "VoxelEngine", nullptr, nullptr);
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

    glViewport(0, 0, 1924, 1024);
    
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
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1924, 1024, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    // Настройки фильтрации (для квада лучше всего GL_LINEAR или GL_NEAREST)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    

    Camera camera;
    camera.SetPerspective(120.0f, 1924 / 1024, 0.1f, 100.0f);
    
    VoxelScene scene;
    
   
    scene.LoadVox("assets/untitled.vox");
    scene.LoadVox("assets/cars.vox");
   
    
    /*for(int x = 0; x< 20;x++)
    {
        for (int z = 0; z < 20;z++)
        {
            
            scene.objects[x * 20 +z].transform.position = glm::vec3(x * 40, 0, z * 40);
        }
    }*/
  
    
    

    EditorUI ui;
    ui.Init(window);
    ui.scene = &scene;
   

    float dt;
    double timer = 0;
    
    
    scene.SplitObject(0);

    // 4. Главный цикл
    while (!glfwWindowShouldClose(window))
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        dt = glfwGetTime() - timer;
        timer = glfwGetTime();
       for(int i = 0;i<scene.objects.size();i++)
        scene.SplitObject(i);
        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
        {
            // Ваши параметры камеры (мировые координаты)
            glm::vec3 cameraPosition = camera.GetPosition();
            glm::vec3 cameraForward = camera.getForward(); // Вектор направления взгляда
            glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f); // Или ваш локальный Up камеры
            float fov = 90.0f; // Угол обзора в градусах (например, 60 или 45)

            // Генерируем луч
            Ray ray = GetRaytracingMouseRay(window, cameraPosition, cameraForward, cameraUp, fov);

            // Пускаем луч в сцену для удаления вокселей
            scene.RemoveVoxelByRay(ray.origin, ray.direction);
        }


        camera.Update(window, dt);
        scene.UpdateTransforms();
        scene.UpdateAndUploadToGpu();
        
        compShader.use();

        compShader.setFloat("time", (float)glfwGetTime());
        compShader.setVec2("uResolution", glm::vec2(1924, 1024));
        compShader.setMatrix4("viewMatrix",camera.GetViewMatrix());
        compShader.setVec3("cameraPos", camera.GetPosition());
        compShader.setInt("NumEntities",scene.objects.size());
       
        //std::cout << camera.GetPosition().x << " " << camera.GetPosition().y << " " << camera.GetPosition().z << std::endl;
        //Биндим текстуру экрана
        glBindImageTexture(0, screenTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

        // Запускаем Compute-шейдер
        glDispatchCompute((1980 + 15) / 16, (1024 + 15) / 16, 1);

        // Обязательный барьер: ждем, пока GPU допишет все пиксели в текстуру
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
