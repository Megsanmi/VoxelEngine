cmake_minimum_required(VERSION 3.20)
project(VoxelEngine)

set(CMAKE_CXX_STANDARD 17)

# --- ГЛОБАЛЬНЫЕ НАСТРОЙКИ ДЛЯ JOLT (Должны быть ДО add_subdirectory) ---
set(TARGET_UNIT_TESTS OFF CACHE BOOL "Disable Jolt tests" FORCE)
set(USE_STATIC_MSVC_RUNTIME OFF CACHE BOOL "Disable /MT for Jolt" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "Disable Jolt samples" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "Disable Jolt viewer" FORCE)
set(LOG_ERRORS_TO_STDOUT ON CACHE BOOL "Log Jolt errors to stdout" FORCE)

# Рекомендуется: включить RTTI в Jolt, чтобы он соответствовал вашему проекту
set(CPP_RTTI_ENABLED ON CACHE BOOL "Enable RTTI in Jolt" FORCE)
# ---------------------------------------------------------------------

# 1. Поиск исходников
file(GLOB_RECURSE ENGINE_SOURCES 
    "src/*.cpp" 
    "src/*.hpp"
    "src/*.h"
)
#
# 2. Создаем исполняемый файл
add_executable(VoxelEngine
    ${ENGINE_SOURCES}
    external/glad/glad.c
)

# Включаем Jolt в проект
add_subdirectory(external/jolt/Build)

# --- ГАРАНТИРОВАННОЕ ИСПРАВЛЕНИЕ ДЛЯ JOLT ---
# Находим таргет Jolt и вручную подменяем флаг /MT на /MD для конфигурации Release
if (MSVC)
    # Настройка свойства для современных CMake (3.15+)
    set_target_properties(Jolt PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    )
    
    # Дополнительный сброс старых строковых флагов, которые Jolt выставляет вручную
    get_target_property(JOLT_COMPILE_OPTIONS Jolt COMPILE_OPTIONS)
    if (JOLT_COMPILE_OPTIONS)
        string(REPLACE "/MT" "/MD" JOLT_COMPILE_OPTIONS "${JOLT_COMPILE_OPTIONS}")
        set_target_properties(Jolt PROPERTIES COMPILE_OPTIONS "${JOLT_COMPILE_OPTIONS}")
    endif()
endif()
# --------------------------------------------

# Привязываем Jolt к вашему движку
target_link_libraries(VoxelEngine PRIVATE Jolt)

# --- Steam API ---
# Указываем CMake, где искать файлы библиотек (.lib) для линковки
target_link_directories(VoxelEngine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/external/steam/lib)
# Указываем точный путь к заголовочным файлам Steam SDK
target_include_directories(VoxelEngine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/external/Steam/include)

# Связываем проект с библиотекой (для Windows x64 используйте steam_api64)
target_link_libraries(VoxelEngine PRIVATE steam_api64)


#ENET
# 1. Включаем папку с ENet в сборку (CMake сам найдёт файлы внутри)
add_subdirectory(external/enet)

# 2. Подключаем собранную библиотеку enet к вашему проекту
target_link_libraries(VoxelEngine PRIVATE enet_static )

# 3. Обязательно для Windows (без этого enet выдаст ошибки линковки сокетов)
if(WIN32)
    target_link_libraries(VoxelEngine PRIVATE winmm ws2_32)
endif()

# GLM
target_include_directories(VoxelEngine PRIVATE
    external/glm
)

# GLAD
target_include_directories(VoxelEngine PRIVATE
    external/glad
)

# ImGui
add_library(imgui STATIC
    "external/imgui/imgui/imgui.cpp"
    "external/imgui/imgui/imgui_draw.cpp"
    "external/imgui/imgui/imgui_tables.cpp"
    "external/imgui/imgui/imgui_widgets.cpp"
    "external/imgui/imgui/imgui_impl_glfw.cpp"
    "external/imgui/imgui/imgui_impl_opengl3.cpp"
)

target_include_directories(imgui PUBLIC
    external/imgui
    external/glad
)

target_link_libraries(VoxelEngine PRIVATE imgui)

# GLFW
add_subdirectory(external/glfw)

# vox
target_include_directories(VoxelEngine PRIVATE
    external/ogtVox
)

# Линковка системных библиотек и GLFW
target_link_libraries(VoxelEngine PRIVATE glfw opengl32)
target_link_libraries(imgui PUBLIC glfw)

set_target_properties(VoxelEngine PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)


# =====================================================================
# --- АВТОМАТИЗАЦИЯ СБОРКИ: РЕСУРСЫ, DLL И STEAM APPID ---
# =====================================================================

# 1. Безопасный поиск и поштучное копирование всех DLL из папки bin к .exe
file(GLOB DLL_FILES "${CMAKE_CURRENT_SOURCE_DIR}/bin/*.dll")

foreach(DLL_PATH ${DLL_FILES})
    add_custom_command(TARGET VoxelEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${DLL_PATH}"
            "$<TARGET_FILE_DIR:VoxelEngine>"
        COMMENT "Копирование зависимости: ${DLL_PATH}"
    )
endforeach()

# 2. Создание правильного файла steam_appid.txt с вашим ID (сейчас тестовый 480)
set(STEAM_APP_ID "480") 

add_custom_command(TARGET VoxelEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "${STEAM_APP_ID}" > "$<TARGET_FILE_DIR:VoxelEngine>/steam_appid.txt"
    COMMENT "Создание файла steam_appid.txt с ID: ${STEAM_APP_ID}"
)

# 3. Кастомная цель для копирования шейдеров
file(GLOB_RECURSE SHADER_FILES "shaders/*")

add_custom_target(CopyShaders ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/shaders"
    "$<TARGET_FILE_DIR:VoxelEngine>/shaders"
    DEPENDS ${SHADER_FILES}
    COMMENT "Copying updated shaders to build directory..."
)

add_dependencies(VoxelEngine CopyShaders)

file(GLOB_RECURSE ASSET_FILES "assets/*") 

add_custom_target(CopyAssets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/assets"              # Твоя исходная папка с ассетами в корне
    "$<TARGET_FILE_DIR:VoxelEngine>/assets"   # Куда копировать (рядом с VoxelEngine.exe)
    DEPENDS ${ASSET_FILES}
    COMMENT "Copying updated assets to build directory..."
)

add_dependencies(VoxelEngine CopyAssets)