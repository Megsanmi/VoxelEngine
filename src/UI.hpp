#pragma once

#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"
#include "Renderer/camera.hpp"
#include "scene.hpp"


class EditorUI
{
public:
    GLFWwindow* window = nullptr;
    VoxelScene* scene = nullptr;

    double timer = 0.0;
    double oldTime = 0.0;
    int frameCount = 0;   
    float fps = 0.0f;

    float windowColor[3] = { 0.5f, 0.6f, 0.5f };

    void Init(GLFWwindow* win)
    {
        window = win;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGui::StyleColorsDark();

        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init("#version 460");
    }

    void BeginFrame()
    {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void EndFrame()
    {
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    void Draw()
    { 
        frameCount++; // Каждый кадр увеличиваем счетчик

        double currentTime = glfwGetTime();
        double passedTime = currentTime - timer;

        // Каждые 0.1 секунды пересчитываем FPS
        if (passedTime >= 0.1)
        {
            // Формула: количество кадров делим на точное прошедшее время
            fps = static_cast<float>(frameCount / passedTime);

            // Сбрасываем счетчик кадров и обновляем таймер интервала
            frameCount = 0;
            timer = currentTime;
        }
        
        oldTime = glfwGetTime();
        
        DrawPanel();
    }

private:
      
    void DrawPanel()
    {
        ImGui::Begin("Scene");

        // Статистика
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "FPS: %.1f", fps);
        ImGui::DragFloat3("Camera pos: ", &scene->camera.position.x);
        ImGui::Text("Objects: %zu", scene->objectIDs.size());

        ImGui::Separator();

        // Список объектов
        for (int i = 0; i < scene->objectIDs.size(); i++)
        {
            ImGui::PushID(i);

            // Подсветка выделенного объекта
            if (scene->selectedObjectIndex == scene->objectIDs[i])
            {
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.5f, 0.8f, 0.8f));
            }

            std::string label = "Object " + std::to_string(scene->objectIDs[i]);

            if (ImGui::Selectable(label.c_str(), scene->selectedObjectIndex == scene->objectIDs[i]))
            {
                scene->selectedObjectIndex = scene->objectIDs[i];
            }

            if (scene->selectedObjectIndex == scene->objectIDs[i])
            {
                ImGui::PopStyleColor();
            }

            ImGui::PopID();
        }

        ImGui::Separator();

        // Инспектор выделенного объекта
        if (scene->selectedObjectIndex >= 0)
        {
            ImGui::Text("Selected: Object %d", scene->selectedObjectIndex);
            scene->manager.GetObject(scene->selectedObjectIndex).transform.drawInspector();
            if (ImGui::Button("deleteobj")) scene->manager.UnregisterObject(scene->selectedObjectIndex);
        }

        ImGui::End();
    }

    
};