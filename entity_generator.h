#pragma once

#include "entity.h"

namespace RaccoonEcs
{
	class EntityGenerator
	{
	public:
		using EntityId = Entity::EntityId;

		EntityGenerator() = default;
		~EntityGenerator() = default;
		EntityGenerator(EntityGenerator&) = delete;
		EntityGenerator& operator=(EntityGenerator&) = delete;
		EntityGenerator(EntityGenerator&&) = delete;
		EntityGenerator& operator=(EntityGenerator&&) = delete;

		[[nodiscard]] EntityId generateNewEntityId()
		{
			// 64 bit size will overflow if we create 1 billion entities per second
			// during 292 years, so I assume there's no need to check for overflow
			return ++mMaxEntityId;
		}

		void registerEntityId(EntityId existingEntity)
		{
			mMaxEntityId = std::max(mMaxEntityId, existingEntity);
		}

		Entity::EntityId getMaxEntityId() const
		{
			return mMaxEntityId;
		}

		void setMaxEntityId(Entity::EntityId maxId)
		{
			mMaxEntityId = maxId;
		}

	private:
		EntityId mMaxEntityId = 0;
	};
} // namespace RaccoonEcs
