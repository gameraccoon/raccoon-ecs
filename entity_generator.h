#pragma once

#include <atomic>

#include "entity.h"

namespace RaccoonEcs
{
	class EntityGenerator
	{
	public:
		EntityGenerator() = default;
		virtual ~EntityGenerator() = default;
		EntityGenerator(EntityGenerator&) = delete;
		EntityGenerator& operator=(EntityGenerator&) = delete;
		EntityGenerator(EntityGenerator&&) = delete;
		EntityGenerator& operator=(EntityGenerator&&) = delete;

		[[nodiscard]] virtual Entity::EntityId generateNewEntityId() = 0;
		virtual void registerEntityId(Entity::EntityId existingEntity) = 0;
		virtual Entity::EntityId getLastEntityId() const = 0;
		virtual void setMaxEntityId(Entity::EntityId maxId) = 0;
	};

	class IncrementalEntityGenerator : public EntityGenerator
	{
	public:
		[[nodiscard]] Entity::EntityId generateNewEntityId() final
		{
			// 64 bit size will overflow if we create 1 billion entities per second
			// during 292 years, so I assume there's no need to check for overflow
			return ++mMaxEntityId;
		}

		void registerEntityId(Entity::EntityId existingEntity) final
		{
			mMaxEntityId = std::max(mMaxEntityId, existingEntity);
		}

		Entity::EntityId getLastEntityId() const final
		{
			return mMaxEntityId;
		}

		void setMaxEntityId(Entity::EntityId maxId) final
		{
			mMaxEntityId = maxId;
		}

	private:
		Entity::EntityId mMaxEntityId = 0;
	};

	class MultithreadedEntityGenerator : public EntityGenerator
	{
	public:
		[[nodiscard]] Entity::EntityId generateNewEntityId() final
		{
			// 64 bit size will overflow if we create 1 billion entities per second
			// during 292 years, so I assume there's no need to check for overflow
			return mMaxEntityId.fetch_add(1, std::memory_order::relaxed);
		}

		void registerEntityId(Entity::EntityId existingEntity) final
		{
			Entity::EntityId previous = mMaxEntityId.load(std::memory_order::relaxed);
			while (previous < existingEntity
				&& !mMaxEntityId.compare_exchange_weak(previous, existingEntity,
					std::memory_order::relaxed, std::memory_order::relaxed))
			{}
		}

		Entity::EntityId getLastEntityId() const final
		{
			return mMaxEntityId.load(std::memory_order::relaxed);
		}

		void setMaxEntityId(Entity::EntityId maxId) final
		{
			mMaxEntityId.store(maxId, std::memory_order::relaxed);
		}

	private:
		std::atomic<Entity::EntityId> mMaxEntityId = 0;
	};
} // namespace RaccoonEcs
