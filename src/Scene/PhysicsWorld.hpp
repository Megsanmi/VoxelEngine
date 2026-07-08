#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>

#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h> // Альтернатива
#include <Jolt/Physics/Constraints/SpringSettings.h>

#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>

#include "../VoxelObject/Voxel.hpp"


namespace Layers {
    static constexpr JPH::ObjectLayer NON_MOVING = 0;
    static constexpr JPH::ObjectLayer MOVING = 1;
    static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
}

class VoxelManager;
class Transform;



class PhysicsSystem {
public:
    PhysicsSystem();
    ~PhysicsSystem();

    // Запрещаем копирование
    PhysicsSystem(const PhysicsSystem&) = delete;
    PhysicsSystem& operator=(const PhysicsSystem&) = delete;

    void Init();
    void Update(float deltaTime, VoxelManager* manager);

    // Интерфейс для создания и управления телами
    JPH::BodyInterface& GetBodyInterface() { return mPhysicsSystem.GetBodyInterface(); }
    JPH::PhysicsSystem& GetPhysicsSystem() { return mPhysicsSystem; }
    JPH::BodyID AddBody(VoxelMap* map, JPH::EMotionType type, glm::vec3 pos, glm::vec3 rot,glm::vec3& offsetCollision = glm::vec3(0));
    JPH::Ref<JPH::CharacterVirtual> PhysicsSystem::CreateCharacter(const JPH::Vec3& spawnPosition);
    void RemoveBody(JPH::BodyID bodyID);

    void SyncBody(JPH::BodyID bodyID,Transform& transform);
    void UpdateVirtualCharacter(JPH::CharacterVirtual* character, const JPH::Vec3& targetVelocity, float deltaTime);

    JPH::Ref<JPH::Shape> CreateShapeFromVoxelObject(VoxelMap* map, int& countVoxels);
    JPH::BodyCreationSettings PhysicsSystem::CreateBodySettings(VoxelMap* map, JPH::EMotionType type, glm::vec3 pos, glm::vec3 rot, glm::vec3& offsetCollision = glm::vec3(0)) ;

    void StartPick(JPH::BodyID bodyID, const JPH::Vec3& hitPoint);
    void MovePick(const JPH::Vec3& newPosition, float deltaTime);
    void EndPick();
    
    bool RaycastPick(const JPH::Vec3& origin, const JPH::Vec3& direction, float maxDist, JPH::BodyID& outBodyID, JPH::Vec3& outHitPoint);

    bool IsPicking() const { return mIsPicking; }

    void ClearAllBodies();
private:
    // Вспомогательные классы Jolt для работы слоев столкновений
    
    class BPLayerInterfaceImpl : public JPH::BroadPhaseLayerInterface {
    public:
        BPLayerInterfaceImpl();
        virtual JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
        virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override;

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        virtual const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override;
#endif
    private:
        JPH::BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS];
    };

    class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter {
    public:
        virtual bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override;
    };

    class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter {
    public:
        virtual bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override;
    };

private:
    bool mIsInitialized = false;

    // Системы Jolt
    JPH::PhysicsSystem                  mPhysicsSystem;
    JPH::TempAllocatorImpl*             mTempAllocator = nullptr;
    JPH::JobSystemThreadPool*           mJobSystem = nullptr;

    // Экземпляры фильтров коллизий
    BPLayerInterfaceImpl                mBpLayerInterface;
    ObjectVsBroadPhaseLayerFilterImpl   mObjectVsBroadPhaseLayerFilter;
    ObjectLayerPairFilterImpl           mObjectLayerPairFilter;

    JPH::BodyID mPickedBodyID;
    JPH::Ref<JPH::Constraint> mPickConstraint;
    JPH::Vec3 mPickOffset; // Смещение от центра масс тела до точки захвата
    bool mIsPicking = false;
    JPH::BodyID mPickDummyBodyID;

    // Константы для выделения памяти движку
    static constexpr uint32_t cMaxBodies = 10240;
    static constexpr uint32_t cNumBodyMutexes = 1024;
    static constexpr uint32_t cMaxBodyPairs = 10240;
    static constexpr uint32_t cMaxContactConstraints = 10240;
};
