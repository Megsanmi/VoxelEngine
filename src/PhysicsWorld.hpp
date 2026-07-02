#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include "PhysicsComponent.hpp"

namespace Layers {
    static constexpr JPH::ObjectLayer NON_MOVING = 0;
    static constexpr JPH::ObjectLayer MOVING = 1;
    static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
}

class PhysicsSystem {
public:
    PhysicsSystem();
    ~PhysicsSystem();

    // Запрещаем копирование
    PhysicsSystem(const PhysicsSystem&) = delete;
    PhysicsSystem& operator=(const PhysicsSystem&) = delete;

    void Init();
    void Update(float deltaTime);

    // Интерфейс для создания и управления телами
    JPH::BodyInterface& GetBodyInterface() { return mPhysicsSystem.GetBodyInterface(); }
    JPH::PhysicsSystem& GetPhysicsSystem() { return mPhysicsSystem; }
    void AddBody(PhysicsComponent* comp);
    void RemoveBody(PhysicsComponent* comp);
    void SyncAllBody();


private:
    std::vector<PhysicsComponent*> mComponents;
    std::mutex mComponentsMutex;
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

    // Константы для выделения памяти движку
    static constexpr uint32_t cMaxBodies = 10240;
    static constexpr uint32_t cNumBodyMutexes = 0;
    static constexpr uint32_t cMaxBodyPairs = 10240;
    static constexpr uint32_t cMaxContactConstraints = 10240;
};
