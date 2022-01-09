#pragma once

#include <unordered_map>
#include <optional>

#include "entity.h"

namespace RaccoonEcs
{
	template<typename ComponentTypeId, typename EntityManagerKeyType>
	struct ScheduledOperationsImpl
	{
		struct SingleAddComponentData
		{
			ComponentTypeId componentTypeId;
			void* component;
		};

		struct ComponentAddData
		{
			Entity entity;
			ComponentTypeId componentTypeId;
			void* component;
		};

		struct ComponentRemoveData
		{
			Entity entity;
			ComponentTypeId componentTypeId;
		};

		std::unordered_map<EntityManagerKeyType, std::vector<Entity>> entitiesToAdd;
		std::unordered_map<EntityManagerKeyType, std::vector<Entity>> entitiesToRemove;
		std::unordered_map<EntityManagerKeyType, std::vector<SingleAddComponentData>> singleComponentsToAdd;
		std::unordered_map<EntityManagerKeyType, std::vector<ComponentTypeId>> singleComponentsToRemove;
		std::unordered_map<EntityManagerKeyType, std::vector<ComponentAddData>> componentsToAdd;
		std::unordered_map<EntityManagerKeyType, std::vector<ComponentRemoveData>> componentsToRemove;
	};

	template<typename ComponentTypeId, typename EntityManagerKeyType>
	using OptionalScheduledOperationsImpl = std::optional<ScheduledOperationsImpl<ComponentTypeId, EntityManagerKeyType>>;
} // namespace RaccoonEcs
