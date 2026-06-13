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
        ImGui::Text("Objects: %zu", scene->objects.size());

        ImGui::Separator();

        // Список объектов
        for (int i = 0; i < scene->objects.size(); i++)
        {
            ImGui::PushID(i);

            // Подсветка выделенного объекта
            if (scene->selectedObjectIndex == i)
            {
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.5f, 0.8f, 0.8f));
            }

            std::string label = "Object " + std::to_string(i);

            if (ImGui::Selectable(label.c_str(), scene->selectedObjectIndex == i))
            {
                scene->selectedObjectIndex = i;
            }

            if (scene->selectedObjectIndex == i)
            {
                ImGui::PopStyleColor();
            }

            ImGui::PopID();
        }

        ImGui::Separator();

        // Инспектор выделенного объекта
        if (scene->selectedObjectIndex >= 0 && scene->selectedObjectIndex < scene->objects.size())
        {
            ImGui::Text("Selected: Object %d", scene->selectedObjectIndex);
            scene->objects[scene->selectedObjectIndex].transform.drawInspector();
        }

        ImGui::End();
    }

    
};