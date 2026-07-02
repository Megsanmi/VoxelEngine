// PhysicsComponent.cpp
#include "PhysicsComponent.hpp"
#include "PhysicsWorld.hpp"
#include "Object.hpp"
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CompoundShape.h>
#include <Jolt/Physics/Body/MassProperties.h>
#include <iostream>

// ============================================================
// СОЗДАНИЕ ФОРМЫ ИЗ ОБЪЕКТА
// ============================================================

JPH::Shape* PhysicsComponent::CreateShapeFromVoxelObject() const
{
    if (!parentObj) return nullptr;

    // Получаем AABB объекта
    glm::vec3 halfExtents = (parentObj->max - parentObj->min) * 0.5f;
    halfExtents = glm::max(halfExtents, glm::vec3(0.001f));

    JPH::Vec3 joltHalfExtents(halfExtents.x, halfExtents.y, halfExtents.z);

    // Создаем BoxShape
    return new JPH::BoxShape(joltHalfExtents);
}

// ============================================================
// СОЗДАНИЕ НАСТРОЕК ТЕЛА
// ============================================================

JPH::BodyCreationSettings PhysicsComponent::CreateBodySettings() const
{
    if (!parentObj) return JPH::BodyCreationSettings();

    JPH::Shape* shape = CreateShapeFromVoxelObject();
    if (!shape) return JPH::BodyCreationSettings();

    // Позиция объекта
    glm::vec3 pos = parentObj->transform.position;
    JPH::Vec3 joltPos(pos.x, pos.y, pos.z);

    // Поворот
    glm::quat q = parentObj->transform.qrotation;
    JPH::Quat joltRot(q.x, q.y, q.z, q.w);

    JPH::BodyCreationSettings settings(
        shape,
        joltPos,
        joltRot,
        motionType,
        Layers::MOVING
    );

    // Настройка массы для динамических тел (НОВЫЙ API)
    if (motionType == JPH::EMotionType::Dynamic) {
        // Создаем MassProperties и вычисляем массу на основе формы
        JPH::MassProperties massProperties;
        massProperties.SetMassAndInertiaOfSolidBox(shape->GetCenterOfMass(), mass);

        settings.mMassPropertiesOverride = massProperties;
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateMassAndInertia;
    }

    return settings;
}

// ============================================================
// ИНИЦИАЛИЗАЦИЯ КОМПОНЕНТА
// ============================================================

// PhysicsComponent.cpp - строка 76
void PhysicsComponent::Init(PhysicsSystem* world)
{
    if (!world || !parentObj || isInitialized) return;

    // Создаем настройки тела
    JPH::BodyCreationSettings settings = CreateBodySettings();

    // Получаем интерфейс тел
    JPH::BodyInterface& bodyInterface = world->GetBodyInterface();

    // Создаем тело
    JPH::Body* body = bodyInterface.CreateBody(settings);

    if (body) {
        bodyID = body->GetID();

        // ============================================================
        // ИСПРАВЛЕНО: Используем Activate вместо DontActivate!
        // ============================================================
        bodyInterface.AddBody(bodyID, JPH::EActivation::Activate);

        // Дополнительно принудительно активируем
        bodyInterface.ActivateBody(bodyID);

        isInitialized = true;
        isDirty = false;

        std::cout << "Body created and activated! ID: " << bodyID.GetIndex() << std::endl;
        std::cout << "Position: " << settings.mPosition.GetX() << ", "
            << settings.mPosition.GetY() << ", "
            << settings.mPosition.GetZ() << std::endl;
    }
}

// ============================================================
// УНИЧТОЖЕНИЕ КОМПОНЕНТА
// ============================================================

void PhysicsComponent::Destroy(PhysicsSystem* world)
{
    if (!world || !isInitialized || bodyID == JPH::BodyID()) return;

    JPH::BodyInterface& bodyInterface = world->GetBodyInterface();

    // Удаляем тело из мира
    bodyInterface.RemoveBody(bodyID);
    bodyInterface.DestroyBody(bodyID);

    bodyID = JPH::BodyID();
    isInitialized = false;
}

// ============================================================
// СИНХРОНИЗАЦИЯ: ИЗ ФИЗИКИ В ОБЪЕКТ
// ============================================================

void PhysicsComponent::SyncTransformFromPhysics(PhysicsSystem* world)
{
    if (!isInitialized || !parentObj || bodyID == JPH::BodyID()) return;

    if (!world) return;
    JPH::BodyInterface& bodyInterface = world->GetBodyInterface();

    // Получаем позицию и поворот из физики
    JPH::Vec3 pos = bodyInterface.GetCenterOfMassPosition(bodyID);
    JPH::Quat rot = bodyInterface.GetRotation(bodyID);

    // Обновляем трансформ объекта
    parentObj->transform.position = glm::vec3(pos.GetX(), pos.GetY(), pos.GetZ());
    parentObj->transform.qrotation = glm::quat(rot.GetW(), rot.GetX(), rot.GetY(), rot.GetZ());
    parentObj->transform.rotationEuler = glm::degrees(glm::eulerAngles(parentObj->transform.qrotation));

    // Помечаем как dirty для обновления GPU
    parentObj->transform.isDirty = true;
    parentObj->isDirty = true;
}

// ============================================================
// СИНХРОНИЗАЦИЯ: ИЗ ОБЪЕКТА В ФИЗИКУ
// ============================================================

void PhysicsComponent::SyncTransformToPhysics(PhysicsSystem* world)
{
    if (!isInitialized || !parentObj || bodyID == JPH::BodyID()) return;

    if (!world) return;

    JPH::BodyInterface& bodyInterface = world->GetBodyInterface();

    // Обновляем позицию
    glm::vec3 pos = parentObj->transform.position;
    bodyInterface.SetPosition(bodyID, JPH::Vec3(pos.x, pos.y, pos.z), JPH::EActivation::DontActivate);

    // Обновляем поворот
    glm::quat q = parentObj->transform.qrotation;
    bodyInterface.SetRotation(bodyID, JPH::Quat(q.x, q.y, q.z, q.w), JPH::EActivation::DontActivate);
}

// ============================================================
// ПЕРЕСОЗДАНИЕ КОЛЛАЙДЕРА
// ============================================================

void PhysicsComponent::OnColliderChanged(PhysicsSystem* world)
{
    if (!world || !isInitialized) return;

    // Пересоздаем тело с новой формой
    Destroy(world);
    Init(world);
}

// ============================================================
// ИЗМЕНЕНИЕ МАССЫ
// ============================================================

void PhysicsComponent::SetMass(PhysicsSystem* world, float newMass)
{
    mass = newMass;
    if (isInitialized && motionType == JPH::EMotionType::Dynamic) {
        // Для динамических тел нужно пересоздать тело
        Destroy(world);
        Init(world);
    }
}

// ============================================================
// ИЗМЕНЕНИЕ ТИПА ДВИЖЕНИЯ
// ============================================================

void PhysicsComponent::SetMotionType(PhysicsSystem* world, JPH::EMotionType newType)
{
    if (motionType == newType) return;

    motionType = newType;
    if (isInitialized) {
        Destroy(world);
        Init(world);
    }
}