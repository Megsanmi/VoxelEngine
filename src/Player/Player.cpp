#include "Player.hpp"
#include "../utils/JPH_glm.hpp"
#include "../utils/PrintVec.hpp"
Player::Player()
{
}

Player::~Player()
{
}

void Player::Update(float dt, CharacterInputState& state)
{
    if (!character) return;

    // ========================================================
    // 1. НАКАПЛИВАЕМ И СОХРАНЯЕМ ПОВОРОТ (ИЗ ДЕЛЬТЫ МЫШИ)
    // ========================================================
    float mouseSensitivity = 0.15f; // Настрой под себя чувствительность мыши

    // Накапливаем углы (они сохраняются в переменной класса player.rotation)
    transform.rotationEuler.y += state.mouseXOffset * mouseSensitivity; // Поворот влево-вправо (Yaw)
    transform.rotationEuler.x += state.mouseYOffset * mouseSensitivity; // Поворот вверх-вниз (Pitch)

    // Ограничиваем взгляд вверх/вниз, чтобы голова не провернулась назад
    if (transform.rotationEuler.x > 89.0f)  transform.rotationEuler.x = 89.0f;
    if (transform.rotationEuler.x < -89.0f) transform.rotationEuler.x = -89.0f;


    // ========================================================
    // 2. ФИЗИКА ЗЕМЛИ И ГРАВИТАЦИЯ (ОСЬ Y)
    // ========================================================
    float mVerticalVelocity = velocity.GetY();
    mIsGrounded = (character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround);

    if (mIsGrounded)
    {
        mVerticalVelocity = 0.0f;

        // Обработка прыжка: если в кадре зажат прыжок, даем импульс вверх
        if (state.wantsJump) {
            mVerticalVelocity = 10.5f;
        }
    }
    else
    {
        float gravityY = -0.f;
        mVerticalVelocity += gravityY * dt;
    }

    // ========================================================
    // 3. ПЕРЕВОД НАПРАВЛЕНИЯ ВВОДА С УЧЕТОМ ПОВОРОТА (WASD)
    // ========================================================
    // Переводим текущий угол поворота вокруг оси Y в радианы для тригонометрии
    float yawRad = glm::radians(transform.rotationEuler.y);
    float sinYaw = sin(yawRad);
    float cosYaw = cos(yawRad);

    glm::vec3 worldMoveDir(0.0f);
    // Матрица вращения вектора ввода вокруг оси Y.
    // Знак минус учитывает направление осей твоей графической камеры (W — вперед)
    worldMoveDir.x = (state.moveDirection.x * cosYaw - state.moveDirection.z * sinYaw);
    worldMoveDir.z = -(state.moveDirection.x * sinYaw + state.moveDirection.z * cosYaw);

    // Умножаем направление на скорость персонажа (например, 10)
    float speed = state.wantsSprint ? 15.0f : 10.0f; // Поддержка бега на Shift
    glm::vec3 horizontalVelocity = worldMoveDir * speed;


    // ========================================================
    // 4. СБОРКА И СИНХРОНИЗАЦИЯ С ГРАФИКОЙ
    // ========================================================
    // Записываем финальную скорость в переменную класса, которую в конце кадра заберет Jolt
    velocity = JPH::Vec3(horizontalVelocity.x, mVerticalVelocity, horizontalVelocity.z);

    // Синхронизируем позицию графического транформа с физической капсулой Jolt
    transform.position = Utils::ToGLM(character->GetPosition());
}


