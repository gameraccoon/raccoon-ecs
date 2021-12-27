#pragma once

#include <vector>
#include <tuple>

#include "entity.h"
#include "msvc_fix.h"

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
			return asyncEntityManager.TEMPLATE_MSVC_FIX mSingleThreadedManagerRef;
		}
	};

	class InnerDataAccessor;

	template<typename... Components>
	class ComponentFilter : public BaseAsyncOperation
	{
		template<typename T>
		friend class AsyncSystemsManager;

	public:
		ComponentFilter(const InnerDataAccessor&) {}

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

	protected:
		ComponentFilter() = default;
	};

	template<typename Component>
	class ComponentAdder : public ComponentFilter<Component>
	{
		template<typename T>
		friend class AsyncSystemsManager;

	public:
		ComponentAdder(const InnerDataAccessor&) {}

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

	protected:
		ComponentAdder() = default;
	};

	template<typename Component>
	class ComponentRemover : public BaseAsyncOperation
	{
		template<typename T>
		friend class AsyncSystemsManager;

	public:
		ComponentRemover(const InnerDataAccessor&) {}

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

	protected:
		ComponentRemover() = default;
	};

	template<typename Component>
	class EntitySelector : public BaseAsyncOperation
	{
		template<typename T>
		friend class AsyncSystemsManager;

	public:
		EntitySelector(const InnerDataAccessor&) {}

		template<typename EntityManagerType>
		bool doesEntityHaveComponent(EntityManagerType& entityManager, Entity entity)
		{
			return this->getSync(entityManager).template doesEntityHaveComponent<Component>(entity);
		}

	protected:
		EntitySelector() = default;
	};

	class EntityAdder : public BaseAsyncOperation
	{
		template<typename T>
		friend class AsyncSystemsManager;

	public:
		EntityAdder(const InnerDataAccessor&) {}

		template<typename EntityManagerType>
		Entity addEntity(EntityManagerType& entityManager) const
		{
			return this->getSync(entityManager).TEMPLATE_MSVC_FIX addEntity();
		}

	protected:
		EntityAdder() = default;
	};

	class EntityRemover : public BaseAsyncOperation
	{
		template<typename T>
		friend class AsyncSystemsManager;

	public:
		EntityRemover(const InnerDataAccessor&) {}

		template<typename EntityManagerType>
		void removeEntity(EntityManagerType& entityManager, Entity entity) const
		{
			this->getSync(entityManager).TEMPLATE_MSVC_FIX removeEntity(entity);
		}

	protected:
		EntityRemover() = default;
	};

	class EntityTransferer : public BaseAsyncOperation
	{
		template<typename T>
		friend class AsyncSystemsManager;

	public:
		EntityTransferer(const InnerDataAccessor&) {}

		template<typename EntityManagerType>
		void transferEntity(EntityManagerType& source, EntityManagerType& target, Entity entity) const
		{
			this->getSync(source).TEMPLATE_MSVC_FIX transferEntityTo(getSync(target), entity);
		}

	protected:
		EntityTransferer() = default;
	};

	class ScheduledActionsExecutor : public BaseAsyncOperation
	{
		template<typename T>
		friend class AsyncSystemsManager;

	public:
		ScheduledActionsExecutor(const InnerDataAccessor&) {}

		template<typename EntityManagerType>
		void executeScheduledActions(EntityManagerType& entityManager) const
		{
			this->getSync(entityManager).TEMPLATE_MSVC_FIX executeScheduledActions();
		}

	protected:
		ScheduledActionsExecutor() = default;
	};

	class InnerDataAccessor : public BaseAsyncOperation
	{
		template<typename T>
		friend class AsyncSystemsManager;

	public:
		template<typename AsyncEntityManagerType>
		auto& getSingleThreadedEntityManager(AsyncEntityManagerType& asyncEntityManager) const
		{
			return this->getSync(asyncEntityManager);
		}

#ifdef RACCOON_ECS_TOOLMODE
	public:
#else
	protected:
#endif // RACCOON_ECS_TOOLMODE
		InnerDataAccessor() = default;
	};
} // namespace RaccoonEcs
