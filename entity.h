#pragma once

#include <cstdint>

#include "error_handling.h"

namespace RaccoonEcs
{
	/**
	 * @brief Class that identifies one entity that can hold different components
	 *
	 * Should be always initialized with valid values
	 */
	class Entity
	{
	public:
		// all the ids are runtime, should not serialize them
		using RawId = std::uint32_t;
		using Version = std::uint32_t;

	public:
		explicit Entity(const RawId id, const Version version) noexcept
			: mId(id)
			, mVersion(version)
		{}

		bool operator==(const Entity& other) const noexcept
		{
			return mId == other.mId && mVersion == other.mVersion;
		}
		bool operator!=(const Entity& other) const noexcept { return !(*this == other); }
		bool operator<(const Entity other) const noexcept
		{
			return mId < other.mId || (mId == other.mId && mVersion < other.mVersion);
		}

		[[nodiscard]] RawId getRawId() const noexcept { return mId; }
		[[nodiscard]] Version getVersion() const noexcept { return mVersion; }

	private:
		RawId mId;
		Version mVersion;
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
		// implicit conversion from Entity
		// ReSharper disable once CppNonExplicitConvertingConstructor
		OptionalEntity(const Entity entity)
			: mEntity(entity), mIsValid(true) {} // NOLINT(*-explicit-constructor)
		explicit OptionalEntity(const Entity::RawId id, const Entity::Version version)
			: mEntity(id, version)
			, mIsValid(true)
		{}

		[[nodiscard]] bool isValid() const { return mIsValid; }

		[[nodiscard]] Entity getEntity() const noexcept
		{
			RACCOON_ECS_ASSERT(mIsValid, "Getting uninitialized entity");
			return mEntity;
		}

		[[nodiscard]] Entity::RawId getRawId() const noexcept
		{
			RACCOON_ECS_ASSERT(mIsValid, "Getting uninitialized entity");
			return mEntity.getRawId();
		}

		[[nodiscard]] Entity::Version getVersion() const noexcept
		{
			RACCOON_ECS_ASSERT(mIsValid, "Getting uninitialized entity");
			return mEntity.getVersion();
		}

		// we can't compare two OptionalEntity objects, but can compare them with Entity
		bool operator==(const Entity& other) const noexcept { return mIsValid && mEntity == other; };
		bool operator!=(const Entity& other) const noexcept { return !(*this == other); };
		friend bool operator==(const Entity& entity, const OptionalEntity& optionalEntity) noexcept { return optionalEntity == entity; };
		friend bool operator!=(const Entity& entity, const OptionalEntity& optionalEntity) noexcept { return optionalEntity != entity; };

	private:
		Entity mEntity{ 0, 0 };
		bool mIsValid = false;
	};

	static_assert(sizeof(Entity) == 8, "Size of Entity changed, make sure this is intentional");
	static_assert(std::is_trivially_copyable<Entity>(), "Entity should be trivially copyable");
	static_assert(std::is_trivially_destructible<Entity>(), "Entity should be trivially destructible");
	static_assert(std::is_trivially_copyable<OptionalEntity>(), "OptionalEntity should be trivially copyable");
	static_assert(std::is_trivially_destructible<OptionalEntity>(), "OptionalEntity should be trivially destructible");
} // namespace RaccoonEcs

template<>
struct std::hash<RaccoonEcs::Entity>
{
	std::size_t operator()(const RaccoonEcs::Entity entity) const noexcept { return std::hash<RaccoonEcs::Entity::RawId>()(entity.getRawId()) ^ std::hash<RaccoonEcs::Entity::Version>()(entity.getVersion()); }
};
