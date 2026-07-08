#pragma once
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

// Чистые данные ввода, очищенные от логики железа (клавиатур или геймпадов)
struct CharacterInputState {
    glm::vec3 moveDirection = glm::vec3(0.0f); // Локальный вектор движения (WASD / Стик)
    float mouseXOffset = 0.0f;                 // Поворот мыши / Правый стик X
    float mouseYOffset = 0.0f;                 // Поворот мыши / Правый стик Y
    bool wantsJump = false;                    // Нажат ли пробел / Кнопка А на геймпаде
    bool wantsSprint = false;                  // Зажат ли Shift / Нажатие на стик
};

class InputController {
private:
    // Храним позиции мыши для расчета дельты (смещения)
    double mLastMouseX = 0.0;
    double mLastMouseY = 0.0;
    bool mFirstMouse = true;
    bool mCursorLocked = false;
public:
    // Опрос клавиатуры и мыши
    CharacterInputState PollKeyboardAndMouse(GLFWwindow* window) {
        CharacterInputState state;

        // ========================================================
        // 1. ПО-ПРОСТОМУ: БЛОКИРОВКА КУРСОРA (TAB / ESCAPE)
        // ========================================================
        if (glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS) {
            mCursorLocked = true;
            mFirstMouse = true; // Сброс, чтобы камера не прыгала при возврате в игру
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            mCursorLocked = false;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }

        // Если курсор не заблокирован (игра на паузе / открыто меню), 
        // просто возвращаем пустой ввод, чтобы персонаж не бегал сам по себе
        if (!mCursorLocked) {
            return state;
        }

        // ========================================================
        // 2. ТВОЙ ИСХОДНЫЙ КОД ОПРОСА КЛАВИАТУРЫ
        // ========================================================
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) state.moveDirection.z += 1.0f;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) state.moveDirection.z -= 1.0f;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) state.moveDirection.x += 1.0f;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) state.moveDirection.x -= 1.0f;

        if (glm::length(state.moveDirection) > 0.0f) {
            state.moveDirection = glm::normalize(state.moveDirection);
        }

        state.wantsJump = (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS);
        state.wantsSprint = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS);

        // ========================================================
        // 3. ТВОЙ ИСХОДНЫЙ КОД ОПРОСА МЫШИ
        // ========================================================
        double mouseX, mouseY;
        glfwGetCursorPos(window, &mouseX, &mouseY);

        if (mFirstMouse) {
            mLastMouseX = mouseX;
            mLastMouseY = mouseY;
            mFirstMouse = false;
        }

        state.mouseXOffset = static_cast<float>(mouseX - mLastMouseX);
        state.mouseYOffset = static_cast<float>(mouseY - mLastMouseY);

        mLastMouseX = mouseX;
        mLastMouseY = mouseY;

        return state;
    }


    // ЗАДЕЛ НА БУДУЩЕЕ: Опрос геймпада
    CharacterInputState PollGamepad(int jid) {
        CharacterInputState state;

        GLFWgamepadstate glfwState;
        if (glfwGetGamepadState(jid, &glfwState)) {
            // Левый стик для ходьбы (оси 0 и 1)
            state.moveDirection.x = glfwState.axes[GLFW_GAMEPAD_AXIS_LEFT_X];
            state.moveDirection.z = -glfwState.axes[GLFW_GAMEPAD_AXIS_LEFT_Y]; // Инверсия оси Z под логику forward

            // Мертвая зона для стиков, чтобы персонаж не дрейфовал сам по себе
            if (glm::length(state.moveDirection) < 0.1f) state.moveDirection = glm::vec3(0.0f);

            // Правый стик для камеры (оси 2 и 3)
            state.mouseXOffset = glfwState.axes[GLFW_GAMEPAD_AXIS_RIGHT_X] * 10.0f; // Умножаем на коэффициент скорости
            state.mouseYOffset = -glfwState.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y] * 10.0f;

            // Кнопка прыжка (например, Кнопка А / Крест)
            state.wantsJump = (glfwState.buttons[GLFW_GAMEPAD_BUTTON_A] == GLFW_PRESS);
        }
        return state;
    }
};
