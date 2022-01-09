#pragma once

#include <vector>
#include <tuple>

#include "entity.h"
#include "async_system.h"
#include "msvc_fix.h"
#include "async_scheduled_operations.h"

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
		template<typename T, typename K>
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
		template<typename T, typename K>
		friend class AsyncSystemsManager;

	public:
		ComponentAdder(const InnerDataAccessor&) {}

		template<typename EntityManagerType, typename ComponentAddDataVector>
		Component* addComponent(EntityManagerType& entityManager, ComponentAddDataVector& data, Entity entity) const
		{
			void* newComponent = this->getSync(entityManager).TEMPLATE_MSVC_FIX createUnmanagedComponentUnsafe(Component::GetTypeId());
			data.TEMPLATE_MSVC_FIX emplace_back(entity, Component::GetTypeId(), newComponent);
			return static_cast<Component*>(newComponent);
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
		template<typename T, typename K>
		friend class AsyncSystemsManager;

	public:
		ComponentRemover(const InnerDataAccessor&) {}

		template<typename ComponentRemoveDataVector>
		void removeComponent(ComponentRemoveDataVector& data, Entity entity) const
		{
			data.TEMPLATE_MSVC_FIX emplace_back(entity, Component::GetTypeId());
		}

	protected:
		ComponentRemover() = default;
	};

	class EntityAdder : public BaseAsyncOperation
	{
		template<typename T, typename K>
		friend class AsyncSystemsManager;

	public:
		EntityAdder(const InnerDataAccessor&) {}

		template<typename EntityManagerType, typename EntityAddDataVector>
		Entity addEntity(EntityManagerType& entityManager, EntityAddDataVector& data) const
		{
			Entity newEntity = entityManager.generateNewEntityUnsafe();
			data.push_back(newEntity);
			return newEntity;
		}

	protected:
		EntityAdder() = default;
	};

	class EntityRemover : public BaseAsyncOperation
	{
		template<typename T, typename K>
		friend class AsyncSystemsManager;

	public:
		EntityRemover(const InnerDataAccessor&) {}

		template<typename EntityRemoveDataVector>
		void removeEntity(Entity entity, EntityRemoveDataVector& data) const
		{
			data.push_back(entity);
		}

	protected:
		EntityRemover() = default;
	};

	class EntityTransferer : public BaseAsyncOperation
	{
		template<typename T, typename K>
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

	class InnerDataAccessor : public BaseAsyncOperation
	{
		template<typename T, typename K>
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
