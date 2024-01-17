#pragma once

#include <vector>

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

		void preallocateForMaxEntitiesCount(size_t count)
		{
			mEntityVersions.reserve(count);
			mFreeEntityIds.reserve(count);
		}

		[[nodiscard]] Entity generateNewEntity()
		{
			if (mFreeEntityIds.empty())
			{
				mEntityVersions.emplace_back(0);
				return Entity(mEntityVersions.size() - 1, 0);
			}
			else
			{
				size_t freeEntityId = mFreeEntityIds.back();
				mFreeEntityIds.pop_back();
				return Entity(freeEntityId, mEntityVersions[freeEntityId]);
			}
		}

		void removeEntity(Entity entity)
		{
			const Entity::Version newVersion = ++mEntityVersions[entity.getRawId()];
			// if we hit zero, we used up all the versions for this entity id, skip it
			if (newVersion != 0)
			{
				mFreeEntityIds.push_back(entity.getRawId());
			}
		}

	private:
		std::vector<Entity::Version> mEntityVersions;
		std::vector<size_t> mFreeEntityIds;
	};
} // namespace RaccoonEcs
