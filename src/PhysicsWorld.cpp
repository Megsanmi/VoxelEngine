#include "PhysicsWorld.hpp"
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>

// Определения для слоев BroadPhase (грубая фаза коллизий)
namespace BPLayers {
    static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
    static constexpr JPH::BroadPhaseLayer MOVING(1);
}

// Реализация методов интерфейса слоев
PhysicsSystem::BPLayerInterfaceImpl::BPLayerInterfaceImpl() {
    mObjectToBroadPhase[Layers::NON_MOVING] = BPLayers::NON_MOVING;
    mObjectToBroadPhase[Layers::MOVING] = BPLayers::MOVING;
}

JPH::BroadPhaseLayer PhysicsSystem::BPLayerInterfaceImpl::GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const {
    return mObjectToBroadPhase[inLayer];
}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
const char* PhysicsSystem::BPLayerInterfaceImpl::GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const {
    if (inLayer == BPLayers::NON_MOVING) return "NON_MOVING";
    if (inLayer == BPLayers::MOVING) return "MOVING";
    return "INVALID";
}
#endif

// Фильтр: Должен ли слой объекта сталкиваться со слоем BroadPhase
bool PhysicsSystem::ObjectVsBroadPhaseLayerFilterImpl::ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const {
    if (inLayer1 == Layers::NON_MOVING) {
        return inLayer2 == BPLayers::MOVING; // Статичные объекты сталкиваются только с динамичными
    }
    return true; // Динамичные сталкиваются со всеми
}

// Фильтр: Должны ли два слоя объектов сталкиваться между собой
bool PhysicsSystem::ObjectLayerPairFilterImpl::ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const {
    if (inObject1 == Layers::NON_MOVING) {
        return inObject2 == Layers::MOVING; // Статика только с динамикой
    }
    return true; // Во всех остальных случаях сталкиваются
}

// Конструктор и деструктор класса PhysicsWorld
PhysicsSystem::PhysicsSystem() {}

PhysicsSystem::~PhysicsSystem() {
    if (mIsInitialized) {
        JPH::UnregisterTypes();
        delete mJobSystem;
        delete mTempAllocator;
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
}

void PhysicsSystem::Init() {
    if (mIsInitialized) return;

    // 1. Инициализация базовых систем Jolt
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    // 2. Выделение памяти под временные структуры и потоки симуляции
    mTempAllocator = new JPH::TempAllocatorImpl(20 * 1024 * 1024); // 10 МБ буфер
    mJobSystem = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, std::thread::hardware_concurrency() - 1);

    // 3. Инициализация физической системы
    mPhysicsSystem.Init(
        cMaxBodies,
        cNumBodyMutexes,
        cMaxBodyPairs,
        cMaxContactConstraints,
        mBpLayerInterface,
        mObjectVsBroadPhaseLayerFilter,
        mObjectLayerPairFilter
    );

    // Опционально: Настройка гравитации (по умолчанию Земная по оси Y)
    mPhysicsSystem.SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));

    mIsInitialized = true;
}

void PhysicsSystem::Update(float deltaTime) {
    if (!mIsInitialized) return;

    // Шаг симуляции. Jolt рекомендует фиксированный шаг (например, 1/60)
    // 1 — количество внутренних подшагов коллизий
    mPhysicsSystem.Update(deltaTime, 1, mTempAllocator, mJobSystem);
}

void PhysicsSystem::AddBody(PhysicsComponent* comp) {
    std::lock_guard<std::mutex> lock(mComponentsMutex);
    if (comp && !comp->isInitialized) {
        comp->Init(this);
        mComponents.push_back(comp);
    }
}

void PhysicsSystem::RemoveBody(PhysicsComponent* comp) {
    std::lock_guard<std::mutex> lock(mComponentsMutex);
    auto it = std::find(mComponents.begin(), mComponents.end(), comp);
    if (it != mComponents.end()) {
        comp->Destroy(this);
        mComponents.erase(it);
    }
}

void PhysicsSystem::SyncAllBody() {
    std::lock_guard<std::mutex> lock(mComponentsMutex);

    for (auto* comp : mComponents) {
        if (comp->parentObj) {
            comp->SyncTransformFromPhysics(this);
            comp->isDirty = false;
        }
    }
}
