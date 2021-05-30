#pragma once

#include <vector>
#include <tuple>

#include "entity.h"

namespace RaccoonEcs
{
	template<typename... Components>
	class ComponentFilter
	{
	public:
		template<typename EntityManagerType, typename... AdditionalData>
		void getComponents(EntityManagerType& entityManager, std::vector<std::tuple<AdditionalData..., Components*...>>& components, AdditionalData... data) const
		{
			entityManager.template getComponents<Components...>(components, data...);
		}

		template<typename EntityManagerType, typename... AdditionalData>
		void getComponentsWithEntities(EntityManagerType& entityManager, std::vector<std::tuple<AdditionalData..., Entity, Components*...>>& components, AdditionalData... data) const
		{
			entityManager.template getComponentsWithEntities<Components...>(components, data...);
		}

		template<typename ComponentSetHolderType>
		std::tuple<Components*...> getComponents(ComponentSetHolderType& componentHolder) const
		{
			return componentHolder.template getComponents<Components...>();
		}

		template<typename EntityManagerType, typename FunctionType, typename... AdditionalData>
		void forEachComponentSet(EntityManagerType& entityManager, FunctionType processor, AdditionalData... data) const
		{
			entityManager.template forEachComponentSet<Components...>(processor, data...);
		}

		template<typename EntityManagerType, typename FunctionType, typename... AdditionalData>
		void forEachComponentSetWithEntity(EntityManagerType& entityManager, FunctionType processor, AdditionalData... data) const
		{
			entityManager.template forEachComponentSetWithEntity<Components...>(processor, data...);
		}

		template<typename ComponentSetHolderType>
		std::tuple<Components*...> getEntityComponents(ComponentSetHolderType& componentHolder, Entity entity) const
		{
			return componentHolder.template getEntityComponents<Components...>(entity);
		}
	};

	template<typename Component>
	class ComponentAdder : public ComponentFilter<Component>
	{
	public:
		template<typename EntityManagerType>
		Component* addComponent(EntityManagerType& entityManager, Entity entity) const
		{
			return entityManager.template addComponent<Component>(entity);
		}

		template<typename EntityManagerType>
		Component* scheduleAddComponent(EntityManagerType& entityManager, Entity entity) const
		{
			return entityManager.template scheduleAddComponent<Component>(entity);
		}

		template<typename EntityViewType>
		Component* scheduleAddComponent(EntityViewType& entityView) const
		{
			return entityView.template scheduleAddComponent<Component>();
		}

		template<typename ComponentHolderType>
		Component* getOrAddComponent(ComponentHolderType& componentHolder) const
		{
			return componentHolder.template getOrAddComponent<Component>();
		}

		template<typename ComponentHolderType>
		Component* addComponent(ComponentHolderType& componentHolder) const
		{
			return componentHolder.template addComponent<Component>();
		}
	};

	template<typename Component>
	class ComponentRemover
	{
	public:
		template<typename EntityManagerType>
		Component* scheduleRemoveComponent(EntityManagerType& entityManager, Entity entity) const
		{
			return entityManager.template scheduleRemoveComponent<Component>(entity);
		}

		template<typename EntityViewType>
		void scheduleRemoveComponent(EntityViewType& entityView) const
		{
			entityView.template scheduleRemoveComponent<Component>();
		}
	};

	class EntityAdder
	{
	public:
		template<typename EntityManagerType>
		Entity addEntity(EntityManagerType& entityManager) const
		{
			return entityManager.template addEntity();
		}
	};

	class EntityRemover
	{
	public:
		template<typename EntityManagerType>
		void removeEntity(EntityManagerType& entityManager, Entity entity) const
		{
			entityManager.template removeEntity(entity);
		}
	};
} // namespace RaccoonEcs
