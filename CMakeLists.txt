cmake_minimum_required(VERSION 3.20)
project(VoxelEngine)

set(CMAKE_CXX_STANDARD 17)

# 1. Автоматически ищем ВСЕ файлы .cpp и .h в папке src и её подпапках
file(GLOB_RECURSE ENGINE_SOURCES 
    "src/*.cpp" 
    "src/*.h"
)

# 2. Создаем исполняемый файл (теперь сюда достаточно передать переменную и glad)
add_executable(VoxelEngine
    ${ENGINE_SOURCES}
    external/glad/glad.c
 "src/scene.cpp" "src/vox_impl.cpp")

# GLM
target_include_directories(VoxelEngine PRIVATE
    external/glm
)

# GLAD
target_include_directories(VoxelEngine PRIVATE
    external/glad
)

# ImGui
add_library(imgui
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

#vox
target_include_directories(VoxelEngine PRIVATE
    external/ogtVox
)

target_link_libraries(VoxelEngine PRIVATE glfw opengl32)

target_link_libraries(imgui PUBLIC glfw)


set_target_properties(VoxelEngine PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

file(GLOB_RECURSE SHADER_FILES "shaders/*")



# 2. Создаем кастомную цель, которая следит за изменениями в файлах
add_custom_target(CopyShaders ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/shaders"
    "$<TARGET_FILE_DIR:VoxelEngine>/shaders"
    DEPENDS ${SHADER_FILES}
    COMMENT "Copying updated shaders to build directory..."
)

# 3. Делаем так, чтобы наш движок зависел от этой цели
add_dependencies(VoxelEngine CopyShaders)