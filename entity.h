#pragma once

#include <cstdint>

#include "error_handling.h"

namespace RaccoonEcs
{
	/**
	 * @brief Class that identifies one entity that can hold different components
	 *
	 * Should be always initialized
	 */
	class Entity
	{
	public:
		using EntityId = std::uint64_t;

	public:
		explicit Entity(EntityId id) : mId(id) {}

		bool operator ==(Entity b) const { return mId == b.mId; }
		bool operator !=(Entity b) const { return !(*this == b); }
		bool operator <(Entity b) const { return mId < b.mId; }

		[[nodiscard]] EntityId getId() const { return mId; }

	private:
		EntityId mId;
	};

	/**
	 * @brief Class that identifies one entity that can hold different components
	 *
	 * Can be default-initialized, in this case `isValid` returns false.
	 *
	 * Can be implicitly converted from Entity
	 *
	 * Note: getEntity and getId should not be called if the entity doesn't have a valid value
	 */
	class OptionalEntity
	{
	public:
		OptionalEntity() = default;
		// implicit conversion
		OptionalEntity(Entity entity) : mId(entity.getId()), mIsValid(true) {}
		explicit OptionalEntity(Entity::EntityId id) : mId(id), mIsValid(true) {}

		[[nodiscard]] bool isValid() const { return mIsValid; }

		[[nodiscard]] Entity getEntity() const noexcept
		{
			RACCOON_ECS_ASSERT(mIsValid, "Getting uninitialized entity");
			return Entity(mId);
		}

		[[nodiscard]] Entity::EntityId getId() const noexcept
		{
			RACCOON_ECS_ASSERT(mIsValid, "Getting uninitialized entity");
			return mId;
		}

	private:
		Entity::EntityId mId = 0;
		bool mIsValid = false;
	};

	static_assert(sizeof(Entity) == 8, "Entity is too big");
	static_assert(std::is_trivially_copyable<Entity>(), "Entity should be trivially copyable");
	static_assert(std::is_trivially_destructible<Entity>(), "Entity should be trivially destructible");
	static_assert(std::is_trivially_copyable<OptionalEntity>(), "OptionalEntity should be trivially copyable");
	static_assert(std::is_trivially_destructible<OptionalEntity>(), "OptionalEntity should be trivially destructible");
} // namespace RaccoonEcs
