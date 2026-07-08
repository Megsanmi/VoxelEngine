
#define GLM_ENABLE_EXPERIMENTAL

#include "PhysicsWorld.hpp"
#include "Manager.hpp"
#include "../VoxelObject/Object.hpp"

#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CompoundShape.h>
#include <Jolt/Physics/Collision/Shape/MutableCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Body/MassProperties.h>

#include <Jolt/Physics/Collision/CastResult.h>

#include <glm/gtx/quaternion.hpp> 

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

void PhysicsSystem::Update(float deltaTime,VoxelManager* manager) {
    if (!mIsInitialized) return;

    for (int i : manager->GetAllObjectIds())
    {
        VoxelObject& obj = manager->GetObject(i);
        if (obj.bodyID != JPH::BodyID())
        {
            SyncBody(obj.bodyID, obj.transform);
        }
    }

    mPhysicsSystem.Update(deltaTime, 1, mTempAllocator, mJobSystem);
}

JPH::BodyID PhysicsSystem::AddBody(VoxelMap* map,JPH::EMotionType type, glm::vec3 pos, glm::vec3 rot, glm::vec3& offsetCollision) {
    JPH::BodyCreationSettings settings = CreateBodySettings(map,type, pos, rot, offsetCollision);

    JPH::BodyInterface& bodyInterface = GetBodyInterface();

    JPH::Body* body = bodyInterface.CreateBody(settings);
    
    JPH::BodyID bodyID;

    if (body) {
        bodyID = body->GetID();

        bodyInterface.AddBody(bodyID, JPH::EActivation::Activate);


        bodyInterface.ActivateBody(bodyID);

        JPH::Vec3 centerMass = bodyInterface.GetCenterOfMassPosition(bodyID);

        offsetCollision = pos-glm::vec3(centerMass.GetX(), centerMass.GetY(), centerMass.GetZ());

        return bodyID;
    }
    return JPH::BodyID();
}

JPH::Ref<JPH::CharacterVirtual> PhysicsSystem::CreateCharacter(const JPH::Vec3& spawnPosition) {
    // 1. Создаем настройки базовой формы (Капсула: полувысота 0.9м, радиус 0.4м -> общая высота 1.8м)
    // Оборачиваем в RotatedTranslatedShape, чтобы сдвинуть центр формы вверх, 
    // и точка (0,0,0) персонажа оказалась у него в ногах.
    JPH::RefConst<JPH::Shape> capsule = new JPH::CapsuleShape(0.5f, 0.4f);
    JPH::RotatedTranslatedShapeSettings shape_settings(JPH::Vec3(0, 0.9f, 0), JPH::Quat::sIdentity(), capsule);
    JPH::Shape::ShapeResult shape_result = shape_settings.Create();

    // 2. Конфигурируем настройки самого виртуального персонажа
    JPH::CharacterVirtualSettings settings;
    settings.mShape = shape_result.Get();
    settings.mMaxSlopeAngle = JPH::DegreesToRadians(45.0f); // Максимальный угол склона, на который можно зайти
    
    // 3. Создаем инстанс персонажа
    return new JPH::CharacterVirtual(&settings, spawnPosition, JPH::Quat::sIdentity(), &mPhysicsSystem);
}


void PhysicsSystem::RemoveBody(JPH::BodyID bodyID) {
    JPH::BodyInterface& bodyInterface = GetBodyInterface();
    bodyInterface.RemoveBody(bodyID); 
}

void PhysicsSystem::SyncBody(JPH::BodyID bodyID, Transform& transform) {
    if (bodyID == JPH::BodyID()) return;

    
    JPH::BodyInterface& bodyInterface = GetBodyInterface();

    JPH::Vec3 pos = bodyInterface.GetCenterOfMassPosition(bodyID);
    JPH::Quat rot = bodyInterface.GetRotation(bodyID);


    transform.position = glm::vec3(pos.GetX(), pos.GetY(), pos.GetZ())+ transform.offsetCollision;
    transform.qrotation = glm::quat(rot.GetW(), rot.GetX(), rot.GetY(), rot.GetZ());
    transform.rotationEuler = glm::degrees(glm::eulerAngles(transform.qrotation));

    transform.isDirty = true;
}

// PhysicsSystem.cpp
void PhysicsSystem::UpdateVirtualCharacter(JPH::CharacterVirtual* character, const JPH::Vec3& targetVelocity, float deltaTime) {
    if (!character) return;

    // 1. Устанавливаем персонажу скорость, которую рассчитал ваш игровой класс
    character->SetLinearVelocity(targetVelocity);

    // 2. Настраиваем параметры шага по ступеням (Step-Up / Step-Down)
    JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
    updateSettings.mWalkStairsStepUp = character->GetUp() * 0.3f;      // Шаг вверх на 30 см
    updateSettings.mStickToFloorStepDown = character->GetUp() * -0.1f; // Прижатие к ступеням при спуске

    // 3. Вызываем симуляцию перемещения с обработкой коллизий
    character->ExtendedUpdate(
        deltaTime,
        JPH::Vec3(0, -9.81,0),
        updateSettings,
        mPhysicsSystem.GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
        mPhysicsSystem.GetDefaultLayerFilter(Layers::MOVING),
        { }, // Фильтр тел (пустой)
        { }, // Фильтр форм (пустой)
        *mTempAllocator // Аллокатор вашей системы
    );
}


JPH::BodyCreationSettings PhysicsSystem::CreateBodySettings(VoxelMap* map, JPH::EMotionType type, glm::vec3 pos, glm::vec3 rot, glm::vec3& offsetCollision) 
{
    // Получаем форму как Ref
    int countVoxels = 1;

    JPH::Ref<JPH::Shape> shape = CreateShapeFromVoxelObject(map, countVoxels);
    if (!shape) return JPH::BodyCreationSettings();

    pos += offsetCollision;

    JPH::Vec3 joltPos(pos.x, pos.y, pos.z);
    glm::quat q = glm::quat(rot);
    JPH::Quat joltRot(q.x, q.y, q.z, q.w);

    JPH::ObjectLayer layer = (type == JPH::EMotionType::Dynamic) ? Layers::MOVING : Layers::NON_MOVING;

    // Используем пустой дефолтный конструктор настроек
    JPH::BodyCreationSettings settings;
    
    // ВАЖНО: Вызов SetShape принудительно заставляет settings удерживать форму в памяти!
    settings.SetShape(shape); 
    
    settings.mPosition = joltPos;
    settings.mRotation = joltRot;
    settings.mMotionType = type;
    settings.mObjectLayer = layer;
   
    if (type == JPH::EMotionType::Dynamic) {
        JPH::MassProperties massProperties = shape->GetMassProperties();
        
        float voxelMass = 0.1; 
        
        massProperties.ScaleToMass(voxelMass * countVoxels);
        
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
        settings.mMassPropertiesOverride = massProperties;
    }

    return settings;
}



JPH::Ref<JPH::Shape> PhysicsSystem::CreateShapeFromVoxelObject(VoxelMap* map,int& countVoxels) 
{
    if (!map) return nullptr;
    if (map->size.x <= 0 || map->size.y <= 0 || map->size.z <= 0) return nullptr;

    bool hasVoxels = false;
    for (int x = 0; x < map->size.x && !hasVoxels; x++) {
        for (int y = 0; y < map->size.y && !hasVoxels; y++) {
            for (int z = 0; z < map->size.z; z++) {
                if (map->GetVoxel(x, y, z).ID != 0) {
                    hasVoxels = true;
                    break;
                }
            }
        }
    }
    if (!hasVoxels) return nullptr;

    JPH::StaticCompoundShapeSettings settings;

    float voxelSize = 0.1f;
    float radius = voxelSize * 0.5f; // Радиус шара = половина размера вокселя
    
    JPH::Vec3 modelCenter = JPH::Vec3(
        (float)map->size.x * radius,
        (float)map->size.y * radius,
        (float)map->size.z * radius
    );

    // Шар вместо куба!
    JPH::Ref<JPH::SphereShape> sphereShape = new JPH::SphereShape(radius);

    // Смещения для проверки соседей (6 направлений)
    const int dx[] = {1, -1, 0, 0, 0, 0};
    const int dy[] = {0, 0, 1, -1, 0, 0};
    const int dz[] = {0, 0, 0, 0, 1, -1};

    for (int x = 0; x < map->size.x; x++) {
        for (int y = 0; y < map->size.y; y++) {
            for (int z = 0; z < map->size.z; z++) {
                if (map->GetVoxel(x, y, z).ID == 0) continue;

                // ПРОВЕРКА: это поверхностный воксель?
                bool isSurface = false;
                
                // Проверяем всех 6 соседей
                for (int i = 0; i < 6; i++) {
                    int nx = x + dx[i];
                    int ny = y + dy[i];
                    int nz = z + dz[i];
                    
                    // Если сосед за границей или пустой - это поверхность
                    if (nx < 0 || nx >= map->size.x ||
                        ny < 0 || ny >= map->size.y ||
                        nz < 0 || nz >= map->size.z ||
                        map->GetVoxel(nx, ny, nz).ID == 0) {
                        isSurface = true;
                        break;
                    }
                }

                // Добавляем ТОЛЬКО поверхностные воксели
                if (isSurface) {
                    JPH::Vec3 localPos(
                        x * voxelSize + radius,
                        y * voxelSize + radius,
                        z * voxelSize + radius
                    );
                    countVoxels++;
                    JPH::Vec3 pos = localPos - modelCenter;
                    settings.AddShape(pos, JPH::Quat::sIdentity(), sphereShape);
                }
            }
        }
    }

    JPH::ShapeSettings::ShapeResult result = settings.Create();
    if (result.IsValid()) {
        return result.Get();
    }

    return nullptr;
}

void PhysicsSystem::StartPick(JPH::BodyID bodyID, const JPH::Vec3& hitPoint) {
    // ЗАЩИТА: Если ID пустой, сразу выходим и ничего не делаем
    if (bodyID.IsInvalid()) return;

    if (mIsPicking) EndPick();

    JPH::BodyInterface& bi = GetBodyInterface();

    // 1. Получаем матрицу трансформации тела, чтобы перевести hitPoint в локальное пространство COM
    JPH::RMat44 bodyTransform = bi.GetCenterOfMassTransform(bodyID);

    // Вычисляем локальную точку клика относительно Центра Масс (COM) удерживаемого тела
    mPickOffset = bodyTransform.Inversed() * hitPoint;
    mPickedBodyID = bodyID;

    // 2. Создаем кинематическое "тело-руку" строго в точке клика
    JPH::BodyCreationSettings dummySettings;
    dummySettings.mPosition = hitPoint;
    dummySettings.mMotionType = JPH::EMotionType::Kinematic;
    dummySettings.mObjectLayer = Layers::MOVING;
    dummySettings.SetShape(new JPH::SphereShape(0.01f));
    dummySettings.mIsSensor = true; // Защита от коллизий с миром

    JPH::Body* dummyBody = bi.CreateBody(dummySettings);
    if (!dummyBody) return;

    mPickDummyBodyID = dummyBody->GetID();

    // Добавляем руку в мир и сразу активируем её
    bi.AddBody(mPickDummyBodyID, JPH::EActivation::Activate);

    // 3. Создаем PointConstraint между телом и "рукой"
    JPH::PointConstraintSettings constraintSettings;
    constraintSettings.mSpace = JPH::EConstraintSpace::LocalToBodyCOM;
    constraintSettings.mPoint1 = mPickOffset;
    constraintSettings.mPoint2 = JPH::Vec3::sZero();

    // БЕЗОПАСНОЕ ПОЛУЧЕНИЕ ССЫЛОК БЕЗ ДЕДЛОКОВ ПОТОКОВ
    JPH::Body* body1 = nullptr;
    JPH::Body* body2 = nullptr;

    {
        JPH::BodyLockWrite lock(mPhysicsSystem.GetBodyLockInterface(), bodyID);
        if (lock.Succeeded()) body1 = &lock.GetBody();
    }
    {
        JPH::BodyLockWrite lock(mPhysicsSystem.GetBodyLockInterface(), mPickDummyBodyID);
        if (lock.Succeeded()) body2 = &lock.GetBody();
    }

    // Если оба тела успешно залочены и существуют в памяти — создаем констрейнт
    if (body1 && body2) {
        mPickConstraint = constraintSettings.Create(*body1, *body2);
        GetPhysicsSystem().AddConstraint(mPickConstraint);

        // Переносим активацию СЮДА. Раз лок успешно заблокировал тело, значит оно 100% валидно
        bi.ActivateBody(bodyID);
        mIsPicking = true;
    }
    else {
        // Если что-то пошло не так (одно из тел не залокалось), зачищаем "руку"
        bi.RemoveBody(mPickDummyBodyID);
        bi.DestroyBody(mPickDummyBodyID);
        mPickDummyBodyID = JPH::BodyID();
        mPickedBodyID = JPH::BodyID();
    }
}


void PhysicsSystem::MovePick(const JPH::Vec3& newPosition, float deltaTime) {
    if (!mIsPicking) return;

    JPH::BodyInterface& bi = GetBodyInterface();

    // ИСПРАВЛЕНО: Прямая установка позиции «руки» в пространстве.
    // Активация EActivation::DontActivate предотвращает лишние пробуждения самой кинематики,
    // но принудительно двигает её в пространстве, заставляя констрейнт тянуть объект.
    bi.SetPositionAndRotation(mPickDummyBodyID, newPosition, JPH::Quat::sIdentity(), JPH::EActivation::DontActivate);

    // Принудительно будим удерживаемое динамическое тело, чтобы оно не уснуло на лету
    if (mPickedBodyID != JPH::BodyID()) {
        bi.ActivateBody(mPickedBodyID);
    }
}


void PhysicsSystem::EndPick() {
    if (!mIsPicking) return;

    // Сначала удаляем констрейнт из системы
    if (mPickConstraint != nullptr) {
        GetPhysicsSystem().RemoveConstraint(mPickConstraint);
        mPickConstraint = nullptr;
    }

    JPH::BodyInterface& bi = GetBodyInterface();

    // Удаляем и уничтожаем вспомогательное кинематическое тело
    if (mPickDummyBodyID != JPH::BodyID()) {
        bi.RemoveBody(mPickDummyBodyID);
        bi.DestroyBody(mPickDummyBodyID);
        mPickDummyBodyID = JPH::BodyID();
    }

    // Будим захваченный объект, чтобы он упал/продолжил движение под силой гравитации
    if (mPickedBodyID != JPH::BodyID()) {
        bi.ActivateBody(mPickedBodyID);
        mPickedBodyID = JPH::BodyID();
    }

    mIsPicking = false;
}

bool PhysicsSystem::RaycastPick(const JPH::Vec3& origin, const JPH::Vec3& direction,
    float maxDist, JPH::BodyID& outBodyID, JPH::Vec3& outHitPoint) {

    JPH::RRayCast ray;
    ray.mOrigin = origin;
    ray.mDirection = direction * maxDist;

    JPH::RayCastResult hit;

    if (mPhysicsSystem.GetNarrowPhaseQuery().CastRay(ray, hit)) {
        JPH::BodyID hitBodyID = hit.mBodyID;
        bool isDynamic = false;

        // ИСПРАВЛЕНО: Ограничиваем область видимости лока. 
        // Лок уничтожится СРАЗУ при выходе за закрывающую фигурную скобку.
        {
            JPH::BodyLockRead lock(mPhysicsSystem.GetBodyLockInterface(), hitBodyID);
            if (lock.Succeeded()) {
                isDynamic = (lock.GetBody().GetMotionType() == JPH::EMotionType::Dynamic);
            }
        } // <- Лок снят здесь!

        if (isDynamic) {
            outBodyID = hitBodyID;
            // Безопасно вычисляем точку, так как все логические проверки завершены
            outHitPoint = ray.GetPointOnRay(hit.mFraction);
            return true;
        }
    }
    return false;
}

void PhysicsSystem::ClearAllBodies()
{
}
