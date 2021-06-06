#pragma once

#include <vector>
#include <tuple>

#include "entity.h"

namespace RaccoonEcs
{
	// non-virtual
	class BaseAsyncOperation
	{
	public:
		BaseAsyncOperation() = default;
		~BaseAsyncOperation() = default;
		BaseAsyncOperation(BaseAsyncOperation&) = delete;
		BaseAsyncOperation& operator=(BaseAsyncOperation&) = delete;
		BaseAsyncOperation(BaseAsyncOperation&&) = default;
		BaseAsyncOperation& operator=(BaseAsyncOperation&&) = default;

	protected:
		template<typename AsyncEntityManager>
		auto& getSync(AsyncEntityManager& asyncEntityManager) const
		{
			return asyncEntityManager.template mSingleThreadedManagerRef;
		}
	};

	template<typename... Components>
	class ComponentFilter : public BaseAsyncOperation
	{
	public:
		template<typename EntityManagerType, typename... AdditionalData>
		void getComponents(EntityManagerType& entityManager, std::vector<std::tuple<AdditionalData..., Components*...>>& components, AdditionalData... data) const
		{
			this->getSync(entityManager).template getComponents<Components...>(components, data...);
		}

		template<typename EntityManagerType, typename... AdditionalData>
		void getComponentsWithEntities(EntityManagerType& entityManager, std::vector<std::tuple<AdditionalData..., Entity, Components*...>>& components, AdditionalData... data) const
		{
			this->getSync(entityManager).template getComponentsWithEntities<Components...>(components, data...);
		}

		template<typename ComponentSetHolderType>
		std::tuple<Components*...> getComponents(ComponentSetHolderType& componentHolder) const
		{
			return componentHolder.template getComponents<Components...>();
		}

		template<typename EntityManagerType, typename FunctionType, typename... AdditionalData>
		void forEachComponentSet(EntityManagerType& entityManager, FunctionType processor, AdditionalData... data) const
		{
			this->getSync(entityManager).template forEachComponentSet<Components...>(processor, data...);
		}

		template<typename EntityManagerType, typename FunctionType, typename... AdditionalData>
		void forEachComponentSetWithEntity(EntityManagerType& entityManager, FunctionType processor, AdditionalData... data) const
		{
			this->getSync(entityManager).template forEachComponentSetWithEntity<Components...>(processor, data...);
		}

		template<typename EntityManagerType>
		std::tuple<Components*...> getEntityComponents(EntityManagerType& entityManager, Entity entity) const
		{
			return this->getSync(entityManager).template getEntityComponents<Components...>(entity);
		}
	};

	template<typename Component>
	class ComponentAdder : public ComponentFilter<Component>
	{
	public:
		template<typename EntityManagerType>
		Component* addComponent(EntityManagerType& entityManager, Entity entity) const
		{
			return this->getSync(entityManager).template addComponent<Component>(entity);
		}

		template<typename EntityManagerType>
		Component* scheduleAddComponent(EntityManagerType& entityManager, Entity entity) const
		{
			return this->getSync(entityManager).template scheduleAddComponent<Component>(entity);
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
	class ComponentRemover : public BaseAsyncOperation
	{
	public:
		template<typename EntityManagerType>
		void scheduleRemoveComponent(EntityManagerType& entityManager, Entity entity) const
		{
			this->getSync(entityManager).template scheduleRemoveComponent<Component>(entity);
		}

		template<typename EntityViewType>
		void scheduleRemoveComponent(EntityViewType& entityView) const
		{
			entityView.template scheduleRemoveComponent<Component>();
		}
	};

	class EntityAdder : public BaseAsyncOperation
	{
	public:
		template<typename EntityManagerType>
		Entity addEntity(EntityManagerType& entityManager) const
		{
			return this->getSync(entityManager).template addEntity();
		}
	};

	class EntityRemover : public BaseAsyncOperation
	{
	public:
		template<typename EntityManagerType>
		void removeEntity(EntityManagerType& entityManager, Entity entity) const
		{
			this->getSync(entityManager).template removeEntity(entity);
		}
	};

	class EntityTransferer : public BaseAsyncOperation
	{
	public:
		template<typename EntityManagerType>
		void transferEntity(EntityManagerType& source, EntityManagerType& target, Entity entity) const
		{
			this->getSync(source).template transferEntityTo(getSync(target), entity);
		}
	};

	class ScheduledActionsExecutor : public BaseAsyncOperation
	{
	public:
		template<typename EntityManagerType>
		void executeScheduledActions(EntityManagerType& entityManager) const
		{
			this->getSync(entityManager).template executeScheduledActions();
		}
	};

	class InnerDataAccessor : public BaseAsyncOperation
	{
	public:
		template<typename AsyncEntityManagerType>
		auto& getSingleThreadedEntityManager(AsyncEntityManagerType& asyncEntityManager) const
		{
			return this->getSync(asyncEntityManager);
		}
	};
} // namespace RaccoonEcs
