#pragma once

#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"
#include "../Renderer/camera.hpp"
#include "../Scene/Scene.hpp"
#include "../NetCode/NetWork.hpp"

class EditorUI
{
public:
    GLFWwindow* window = nullptr;
    VoxelScene* scene = nullptr;
    NetworkManager* network = nullptr;
        
    double timer = 0.0;
    double oldTime = 0.0;
    int frameCount = 0;
    float fps = 0.0f;

    float windowColor[3] = { 0.5f, 0.6f, 0.5f };

    // Настройки UI
    bool showStatsWindow = true;
    bool showObjectList = true;
    bool showInspector = true;
    bool showToolbar = true;
    bool showConsole = true;
    bool showAbout = false;
    bool showImportWindow = false;  // Новое окно импорта

    // Переменные для импорта моделей
    char modelPath[256] = "assets/untitled.vox";  // Буфер для пути к модели
    std::string lastLoadedModel = "";  // Последняя загруженная модель

    std::deque<std::string> consoleHistory;
    char consoleInput[256] = "";


    void Init(GLFWwindow* win)
    {
        window = win;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        SetupStyle();

        ImGuiIO& io = ImGui::GetIO();

        // Загрузка шрифта с поддержкой кириллицы
        const char* fontPath  = "assets/fonts/Roboto.ttf";
       
        ImFontConfig fontConfig;
        fontConfig.OversampleH = 4;
        fontConfig.OversampleV = 4;
        fontConfig.PixelSnapH = true;

        ImFont* font = io.Fonts->AddFontFromFileTTF(fontPath, 16.0f,  &fontConfig, io.Fonts->GetGlyphRangesCyrillic());
        
        if (!font)
        {
            font = io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/arial.ttf", 16.0f, NULL, io.Fonts->GetGlyphRangesCyrillic());
        }
    
        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init("#version 460");
    }

    void SetupStyle()
    {
        ImGuiStyle& style = ImGui::GetStyle();

        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
        style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
        style.Colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
        style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);

        style.Colors[ImGuiCol_Header] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
        style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);

        style.Colors[ImGuiCol_Button] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);

        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
        style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
        style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);

        style.Colors[ImGuiCol_CheckMark] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
        style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
        style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.40f, 0.70f, 1.00f, 1.00f);

        style.Colors[ImGuiCol_Separator] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
        style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
        style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);

        style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
        style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
        style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
        style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);

        style.Colors[ImGuiCol_Tab] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
        style.Colors[ImGuiCol_TabHovered] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
        style.Colors[ImGuiCol_TabActive] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);

        style.Colors[ImGuiCol_Text] = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
        style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);

        style.WindowRounding = 4.0f;
        style.FrameRounding = 3.0f;
        style.GrabRounding = 3.0f;
        style.TabRounding = 3.0f;
        style.ScrollbarRounding = 3.0f;
        style.ChildRounding = 3.0f;
        style.PopupRounding = 3.0f;

        style.WindowPadding = ImVec2(8.0f, 8.0f);
        style.FramePadding = ImVec2(6.0f, 4.0f);
        style.ItemSpacing = ImVec2(8.0f, 6.0f);
        style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
        style.IndentSpacing = 21.0f;
    }

    void SetupCyberpunkStyle()
    {
        ImGuiStyle& style = ImGui::GetStyle();

        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.05f, 0.02f, 0.08f, 1.00f);
        style.Colors[ImGuiCol_ChildBg] = ImVec4(0.06f, 0.03f, 0.10f, 1.00f);
        style.Colors[ImGuiCol_PopupBg] = ImVec4(0.06f, 0.03f, 0.10f, 1.00f);

        ImVec4 neonPink = ImVec4(1.0f, 0.0f, 0.8f, 1.0f);
        ImVec4 neonBlue = ImVec4(0.0f, 0.8f, 1.0f, 1.0f);

        style.Colors[ImGuiCol_Button] = ImVec4(0.10f, 0.05f, 0.15f, 1.00f);
        style.Colors[ImGuiCol_ButtonHovered] = neonPink;
        style.Colors[ImGuiCol_ButtonActive] = neonBlue;

        style.Colors[ImGuiCol_Header] = ImVec4(0.10f, 0.05f, 0.15f, 1.00f);
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.20f, 0.10f, 0.30f, 1.00f);
        style.Colors[ImGuiCol_HeaderActive] = neonPink;

        style.Colors[ImGuiCol_CheckMark] = neonPink;
        style.Colors[ImGuiCol_SliderGrab] = neonPink;
        style.Colors[ImGuiCol_SliderGrabActive] = neonBlue;

        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.08f, 0.04f, 0.12f, 1.00f);
        style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.12f, 0.06f, 0.18f, 1.00f);
        style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.15f, 0.08f, 0.22f, 1.00f);

        style.Colors[ImGuiCol_Text] = ImVec4(0.8f, 0.8f, 1.0f, 1.00f);
        style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.4f, 0.4f, 0.6f, 1.00f);

        style.WindowRounding = 0.0f;
        style.FrameRounding = 2.0f;
        style.GrabRounding = 2.0f;
        style.TabRounding = 0.0f;
        style.ScrollbarRounding = 0.0f;

        style.WindowBorderSize = 2.0f;
        style.FrameBorderSize = 1.0f;
        style.TabBorderSize = 1.0f;
    }
    
    void BeginFrame()
    {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("New Scene", "Ctrl+N")) 
                {
                    scene->ClearScene();
                };
                ImGui::MenuItem("Open Scene...", "Ctrl+O");
                ImGui::MenuItem("Save Scene", "Ctrl+S");
                ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S");
                ImGui::Separator();
                
                // Добавляем пункт импорта модели
                if (ImGui::MenuItem("Import Model...", "Ctrl+I"))
                {
                    showImportWindow = true;
                }
                
                ImGui::Separator();
                ImGui::MenuItem("Exit", "Alt+F4");
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Edit"))
            {
                ImGui::MenuItem("Undo", "Ctrl+Z");
                ImGui::MenuItem("Redo", "Ctrl+Y");
                ImGui::Separator();
                ImGui::MenuItem("Cut", "Ctrl+X");
                ImGui::MenuItem("Copy", "Ctrl+C");
                ImGui::MenuItem("Paste", "Ctrl+V");
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View"))
            {
                ImGui::MenuItem("Stats", NULL, &showStatsWindow);
                ImGui::MenuItem("Object List", NULL, &showObjectList);
                ImGui::MenuItem("Inspector", NULL, &showInspector);
                ImGui::MenuItem("Toolbar", NULL, &showToolbar);
                ImGui::MenuItem("Console", NULL, &showConsole);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Help"))
            {
                ImGui::MenuItem("About", NULL, &showAbout);
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }
    }

    void EndFrame()
    {
        // About popup
        if (showAbout)
        {
            ImGui::OpenPopup("About");
            showAbout = false;
        }

        if (ImGui::BeginPopupModal("About", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Voxel Editor v1.0");
            ImGui::Separator();
            ImGui::Text("A 3D voxel scene editor built with:");
            ImGui::Text("  - OpenGL 4.6");
            ImGui::Text("  - GLFW");
            ImGui::Text("  - Dear ImGui");
            ImGui::Separator();
            if (ImGui::Button("Close", ImVec2(120, 0)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    void Draw()
    {
        frameCount++;

        double currentTime = glfwGetTime();
        double passedTime = currentTime - timer;

        if (passedTime >= 0.1)
        {
            fps = static_cast<float>(frameCount / passedTime);
            frameCount = 0;
            timer = currentTime;
        }

        oldTime = glfwGetTime();

        if (showToolbar) DrawToolbar();
        if (showStatsWindow) DrawStatsWindow();
        if (showObjectList) DrawObjectList();
        if (showInspector) DrawInspector();
        if (showConsole) DrawConsole();
        if (showImportWindow) DrawImportWindow();  // Добавляем отрисовку окна импорта
    }
   std::string GetModelPath() const
    {
        return std::string(modelPath);
    }
   void AddConsoleMessage(const std::string& msg)
   {
       consoleHistory.push_back(msg);
       if (consoleHistory.size() > 100)
       {
           consoleHistory.pop_front();
       }
   }
    // Метод для установки пути к модели извне
    void SetModelPath(const std::string& path)
    {
        strcpy_s(modelPath, path.c_str());
    }

    // Метод для получения последней загруженной модели
    std::string GetLastLoadedModel() const
    {
        return lastLoadedModel;
    }
private:

    void DrawToolbar()
    {
        ImGui::SetNextWindowSize(ImVec2(400, 60), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(30, 25), ImGuiCond_FirstUseEver);

        ImGui::Begin("Toolbar", &showToolbar,
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));

        if (ImGui::Button("Select", ImVec2(70, 35))) { /* Ваш код */ }
        ImGui::SameLine();
        if (ImGui::Button("Move", ImVec2(70, 35))) { /* Ваш код */ }
        ImGui::SameLine();
        if (ImGui::Button("Rotate", ImVec2(70, 35))) { /* Ваш код */ }
        ImGui::SameLine();
        if (ImGui::Button("Scale", ImVec2(70, 35))) { /* Ваш код */ }
        ImGui::SameLine();
        if (ImGui::Button("Paint", ImVec2(70, 35))) { /* Ваш код */ }

        ImGui::PopStyleColor();
        ImGui::End();
    }

    void DrawStatsWindow()
    {
        ImGui::SetNextWindowSize(ImVec2(200, 120), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(10, 80), ImGuiCond_FirstUseEver);

        ImGui::Begin("Stats", &showStatsWindow);
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "FPS: %.1f", fps);
        ImGui::Text("Frame time: %.2f ms", 1000.0f / fps);
        ImGui::Separator();
        ImGui::Text("Objects: %zu", scene->objectIDs.size());
        ImGui::Text("Selected: %d", scene->selectedObjectIndex);
        ImGui::End();
    }

    void DrawObjectList()
    {
        ImGui::SetNextWindowSize(ImVec2(250, 400), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(10, 210), ImGuiCond_FirstUseEver);

        ImGui::Begin("Object List", &showObjectList);

        static char searchBuffer[128] = "";
        ImGui::InputTextWithHint("Search", "Filter objects...", searchBuffer, sizeof(searchBuffer));

        ImGui::Separator();

        ImGui::BeginChild("ObjectListChild", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));

        for (int i = 0; i < scene->objectIDs.size(); i++)
        {
            std::string label = "Object " + std::to_string(scene->objectIDs[i]);

            if (strlen(searchBuffer) > 0)
            {
                if (label.find(searchBuffer) == std::string::npos)
                    continue;
            }

            ImGui::PushID(i);

            bool isSelected = (scene->selectedObjectIndex == scene->objectIDs[i]);

            ImGui::TextColored(ImVec4(0.3f, 0.7f, 0.3f, 1.0f), "■");
            ImGui::SameLine();

            if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick))
            {
                scene->selectedObjectIndex = scene->objectIDs[i];
            }

            if (ImGui::BeginPopupContextItem())
            {
                if (ImGui::MenuItem("Delete"))
                {
                    scene->manager.UnregisterObject(scene->objectIDs[i]);
                }
                ImGui::EndPopup();
            }

            ImGui::PopID();
        }

        ImGui::EndChild();

        ImGui::Separator();

        if (ImGui::Button("Add Object", ImVec2(-1, 0)))
        {
            // Ваш код
        }

        ImGui::End();
    }

    void DrawInspector()
    {
        ImGui::SetNextWindowSize(ImVec2(300, 400), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 320, 25), ImGuiCond_FirstUseEver);

        ImGui::Begin("Inspector", &showInspector);

        if (scene->selectedObjectIndex >= 0)
        {
            auto& obj = scene->manager.GetObject(scene->selectedObjectIndex);

            ImGui::TextColored(ImVec4(0.3f, 0.7f, 0.3f, 1.0f), "■");
            ImGui::SameLine();
            ImGui::Text("Object %d", scene->selectedObjectIndex);

            ImGui::Separator();

            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
            {
                obj.transform.drawInspector();
            }

            ImGui::Separator();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            if (ImGui::Button("Delete Object", ImVec2(-1, 30)))
            {
                scene->manager.UnregisterObject(scene->selectedObjectIndex);
                scene->selectedObjectIndex = -1;
            }
            ImGui::PopStyleColor();
        }
        else
        {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No object selected");
        }

        ImGui::End();
    }
    
    void DrawConsole()
    {
        ImGui::SetNextWindowSize(ImVec2(600, 200), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(10, ImGui::GetIO().DisplaySize.y - 230), ImGuiCond_FirstUseEver);

        ImGui::Begin("Console", &showConsole);

        // ============================================================
        // ОБЛАСТЬ СООБЩЕНИЙ
        // ============================================================
        ImGui::BeginChild("ConsoleChild", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), true);

        // Рисуем ВСЕ сообщения из истории
        for (const auto& msg : consoleHistory)
        {
            ImGui::TextWrapped("%s", msg.c_str());
        }

        // Автопрокрутка вниз
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10.0f)
        {
            ImGui::SetScrollHereY(1.0f);
        }

        ImGui::EndChild();

        ImGui::Separator();

        // ============================================================
        // ПОЛЕ ВВОДА
        // ============================================================
        bool enterPressed = ImGui::InputText(
            "##ConsoleInput",
            consoleInput,
            sizeof(consoleInput),
            ImGuiInputTextFlags_EnterReturnsTrue
        );

        ImGui::SameLine();

        if (ImGui::Button("Send", ImVec2(60, 0)) || enterPressed)
        {
            if (strlen(consoleInput) > 0)
            {
                std::string cmd = consoleInput;
                consoleInput[0] = '\0';

                // ============================================================
                // ОБРАБОТКА КОМАНД
                // ============================================================
                if (cmd == "/clear")
                {
                    consoleHistory.clear();
                    AddConsoleMessage("[System] Console cleared");
                }
                else if (cmd == "/help")
                { 
                    AddConsoleMessage("[System] Commands: /help, /clear, /players, /create lobby");
                }
                // Проверяем, что команда начинается с "/host"
                if (cmd.rfind("/host", 0) == 0)
                {
                    std::stringstream ss(cmd);
                    std::string commandName;
                    uint16_t port = 0; // Сюда запишем порт

                    // 1. Считываем первое слово (саму команду "/host")
                    ss >> commandName;

                    // 2. Пробуем считать второе слово как число (порт)
                    if (ss >> port)
                    {
                        AddConsoleMessage("[System] Server adress: " + network->GetLocalIP() + ":" + std::to_string(port));
                        network->StartServer(port,10);
                        
                    }
                    else
                    {
                        // Если после /host ничего нет или там написаны буквы вместо цифр
                        AddConsoleMessage("[System] error: enter port. sample: /host 7777");
                    }
                }

                else if (cmd.rfind("/connect", 0) == 0)
                {
                    std::stringstream ss(cmd);
                    std::string commandName;
                    std::string ipStr;
                    int port = 0;

                    // 1. Read the first word ("/connect")
                    ss >> commandName;

                    // 2. Read the IP address and port
                    if (ss >> ipStr && ss >> port)
                    {
                        AddConsoleMessage("[System] Connecting to " + ipStr + ":" + std::to_string(port) + "...");

                        network->Connect(ipStr, port);
                    }
                    else
                    {
                        // Missing or invalid parameters
                        AddConsoleMessage("[System] Error: specify IP and port. Example: /connect 127.0.0.1 7777");
                    }
                }


                else if (cmd == "/create lobby")
                {
                    //network->CreateSession("My Game", 8);
                    //network->CreateLobby();
                    AddConsoleMessage("[System]  steam no working");
                }

                else
                {
                    // Формируем тело пакета: [ID пакета] + [текст сообщения]
                    std::string packetData;
                    packetData.push_back(static_cast<char>(PacketType::ChatMessage));
                    packetData.append(cmd);

                    network->SendPacket(packetData.data(), packetData.size(),true);

                    // Сразу пишем себе в локальный чат, чтобы видеть свое сообщение
                    AddConsoleMessage("[You]: " + cmd);
                }
            }
        }

        ImGui::End();
    }

    // Новое окно для импорта моделей
    void DrawImportWindow()
    {
        ImGui::SetNextWindowSize(ImVec2(500, 200), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(
            (ImGui::GetIO().DisplaySize.x - 500) * 0.5f,
            (ImGui::GetIO().DisplaySize.y - 200) * 0.5f
        ), ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("Import Model", &showImportWindow, ImGuiWindowFlags_NoCollapse))
        {
            ImGui::End();
            return;
        }

        ImGui::Text("Enter path to 3D model file:");
        ImGui::Spacing();

        // Поле ввода пути
        ImGui::InputText("##ModelPath", modelPath, sizeof(modelPath));
        
        // Кнопка для открытия диалога выбора файла (имитация)
        ImGui::SameLine();
        if (ImGui::Button("Browse..."))
        {
            // Здесь можно открыть системный диалог выбора файла
            // Для примера просто заполним тестовым путем
            strcpy_s(modelPath, "assets/models/sample.obj");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Информация о поддерживаемых форматах
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Supported formats: .obj, .fbx, .gltf, .glb");

        // Отображение последней загруженной модели
        if (!lastLoadedModel.empty())
        {
            ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.3f, 1.0f), "Last loaded: %s", lastLoadedModel.c_str());
        }

        ImGui::Spacing();

        // Кнопки управления
        if (ImGui::Button("Load Model", ImVec2(120, 0)))
        {
            if (strlen(modelPath) > 0)
            {
                // Сохраняем путь в переменную
                lastLoadedModel = std::string(modelPath);
                
                // Здесь ваш код для загрузки модели
                // Например: scene->LoadModel(modelPath);
                
                // Выводим сообщение в консоль (если есть)
                printf("Loading model from: %s\n", modelPath);
                
                // Закрываем окно после загрузки
                showImportWindow = false;
            }
            else
            {
                // Если путь пустой - показываем предупреждение
                ImGui::OpenPopup("Error");
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(120, 0)))
        {
            showImportWindow = false;
        }

        // Обработка ошибки при пустом пути
        if (ImGui::BeginPopupModal("Error", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Please enter a valid path to the model file.");
            ImGui::Separator();
            if (ImGui::Button("OK", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::End();
    }

    // Метод для получения пути к модели извне
 
};