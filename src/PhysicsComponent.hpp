#pragma once


#include <glm/glm.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyInterface.h>


class PhysicsSystem;
class VoxelObject;

struct PhysicsComponent
{
public:
    JPH::BodyID bodyID;
    bool isDirty = false;
    JPH::EMotionType motionType = JPH::EMotionType::Dynamic;
    VoxelObject* parentObj = nullptr;
    float mass = 1.0f;
    bool isInitialized = false;

    // Конструктор
    PhysicsComponent() = default;
    PhysicsComponent(VoxelObject* obj, JPH::EMotionType type = JPH::EMotionType::Dynamic)
        : parentObj(obj), motionType(type) {
    }

    // Основные методы
    void Init(PhysicsSystem* world);
    
    void Destroy(PhysicsSystem* world);
    void SyncTransformFromPhysics(PhysicsSystem* world);
    
    void SyncTransformToPhysics(PhysicsSystem* world);
    void OnColliderChanged(PhysicsSystem* world);

    void SetMass(PhysicsSystem* world, float newMass);
    void SetMotionType(PhysicsSystem* world, JPH::EMotionType newType);

    bool IsValid() const { return bodyID != JPH::BodyID(); }

private:
    JPH::Shape* CreateShapeFromVoxelObject() const;
    JPH::BodyCreationSettings CreateBodySettings() const;
};
