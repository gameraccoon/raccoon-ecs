#pragma once

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
} // namespace RaccoonEcs
