#pragma once
#include <cstdint>


#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include "../VoxelObject/Object.hpp"
#include "PlayerController.hpp"
class Player
{
public:
	uint32_t objID;
	JPH::BodyID bodyID;
	uint32_t netWorkID;

	Transform transform;
	InputController* controller;
	JPH::Vec3 velocity = JPH::Vec3(0,0,0);

	bool mIsGrounded;

	JPH::Ref<JPH::CharacterVirtual> character;

	Player();
	~Player();

	void Update(float dt, CharacterInputState& state);
	glm::vec3 Player::GetEyePosition() const {
		return transform.position + glm::vec3(0.0f, 1.65f, 0.0f); // Позиция ног + высота глаз
	}
	glm::vec3 Player::GetRotation() const {
		return transform.rotationEuler;
	}
private:

};

