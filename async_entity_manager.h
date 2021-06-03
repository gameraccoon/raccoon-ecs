#pragma once

#include <algorithm>
#include <ranges>
#include <tuple>
#include <unordered_map>

#include "entity_manager.h"

namespace RaccoonEcs
{
	template <typename ComponentTypeId>
	class AsyncEntityManagerImpl
	{
		template<typename... Components> friend class ComponentFilter;
		template<typename Component> friend class ComponentAdder;
		template<typename Component> friend class ComponentRemover;
		friend class EntityAdder;
		friend class EntityRemover;
		friend class EntityTransferer;
		friend class ScheduledActionsExecutor;
		friend class InnerDataAccessor;

	public:
		using AsyncEntityManager = AsyncEntityManagerImpl<ComponentTypeId>;
		using EntityManager = EntityManagerImpl<ComponentTypeId>;

		using TypedComponent = EntityManager::TypedComponent;
		using ConstTypedComponent = EntityManager::ConstTypedComponent;

	public:
		AsyncEntityManagerImpl(EntityManager& singleThreadedManagerRef)
			: mSingleThreadedManagerRef(singleThreadedManagerRef)
		{}

	private:
		Entity addEntity()
		{
			return mSingleThreadedManagerRef.addEntity();
		}

		void removeEntity(Entity entityToRemove)
		{
			mSingleThreadedManagerRef.removeEntity(entityToRemove);
		}

		bool hasEntity(Entity entity)
		{
			return mSingleThreadedManagerRef.hasEntity(entity);
		}

		void getAllEntityComponents(Entity entity, std::vector<TypedComponent>& outComponents)
		{
			mSingleThreadedManagerRef.getAllEntityComponents(entity, outComponents);
		}

		void getAllEntityComponents(Entity entity, std::vector<ConstTypedComponent>& outComponents) const
		{
			mSingleThreadedManagerRef.getAllEntityComponents(entity, outComponents);
		}

		[[nodiscard]] bool doesEntityHaveComponent(Entity entity, ComponentTypeId typeId) const
		{
			return mSingleThreadedManagerRef.doesEntityHaveComponent(entity, typeId);
		}

		template<typename ComponentType>
		[[nodiscard]] bool doesEntityHaveComponent(Entity entity)
		{
			return mSingleThreadedManagerRef.template doesEntityHaveComponent<ComponentType>(entity);
		}

		template<typename ComponentType>
		ComponentType* addComponent(Entity entity)
		{
			return mSingleThreadedManagerRef.template addComponent<ComponentType>(entity);
		}

		void* addComponentByType(Entity entity, ComponentTypeId typeId)
		{
			return mSingleThreadedManagerRef.addComponentByType(entity, typeId);
		}

		void addComponent(Entity entity, void* component, ComponentTypeId typeId)
		{
			mSingleThreadedManagerRef.addComponent(entity, component, typeId);
		}

		template<typename ComponentType>
		void removeComponent(Entity entity)
		{
			mSingleThreadedManagerRef.template removeComponent<ComponentType>(entity);
		}

		void removeComponent(Entity entity, ComponentTypeId typeId)
		{
			mSingleThreadedManagerRef.removeComponent(entity, typeId);
		}

		template<typename ComponentType>
		ComponentType* scheduleAddComponent(Entity entity)
		{
			return mSingleThreadedManagerRef.template scheduleAddComponent<ComponentType>(entity);
		}

		void scheduleAddComponent(Entity entity, void* component, ComponentTypeId typeId)
		{
			mSingleThreadedManagerRef.scheduleAddComponent(entity, component, typeId);
		}

		template<typename ComponentType>
		void scheduleRemoveComponent(Entity entity)
		{
			mSingleThreadedManagerRef.template scheduleRemoveComponent<ComponentType>(entity);
		}

		void scheduleRemoveComponent(Entity entity, ComponentTypeId typeId)
		{
			mSingleThreadedManagerRef.scheduleRemoveComponent(entity, typeId);
		}

		void executeScheduledActions()
		{
			mSingleThreadedManagerRef.executeScheduledActions();
		}

		template<typename... Components>
		std::tuple<Components*...> getEntityComponents(Entity entity)
		{
			return mSingleThreadedManagerRef.template getEntityComponents<Components...>(entity);
		}

		template<typename... Components, typename... AdditionalData>
		void getComponents(std::vector<std::tuple<AdditionalData..., Components* ...>>& inOutComponents, AdditionalData... data)
		{
			mSingleThreadedManagerRef.template getComponents<Components...>(inOutComponents, data...);
		}

		template<typename... Components, typename... AdditionalData>
		void getComponentsWithEntities(std::vector<std::tuple<AdditionalData..., Entity, Components* ...>>& inOutComponents, AdditionalData... data)
		{
			mSingleThreadedManagerRef.template getComponentsWithEntities<Components...>(inOutComponents, data...);
		}

		template<typename... Components, typename FunctionType, typename... AdditionalData>
		void forEachComponentSet(FunctionType processor, AdditionalData... data)
		{
			mSingleThreadedManagerRef.template forEachComponentSet<Components...>(processor, data...);
		}

		template<typename... Components, typename FunctionType, typename... AdditionalData>
		void forEachComponentSetWithEntity(FunctionType processor, AdditionalData... data)
		{
			mSingleThreadedManagerRef.template forEachComponentSetWithEntity<Components...>(processor, data...);
		}

		void getEntitiesHavingComponents(const std::vector<ComponentTypeId>& componentIndexes, std::vector<Entity>& inOutEntities) const
		{
			mSingleThreadedManagerRef.getEntitiesHavingComponents(componentIndexes, inOutEntities);
		}

		void transferEntityTo(AsyncEntityManager& otherManager, Entity entity)
		{
			mSingleThreadedManagerRef.transferEntityTo(otherManager.mSingleThreadedManagerRef, entity);
		}

	private:
		EntityManager& mSingleThreadedManagerRef;
	};

} // namespace RaccoonEcs
