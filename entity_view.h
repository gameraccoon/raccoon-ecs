#pragma once

#include "entity.h"
#include "entity_manager.h"
#include "async_entity_manager.h"
#include "msvc_fix.h"

namespace RaccoonEcs
{
	/**
	 * @brief Non-owning wrapper around entity and its current entity manager
	 */
	template <typename ComponentTypeId>
	class EntityViewImpl
	{
	public:
		using EntityManager = EntityManagerImpl<ComponentTypeId>;

		EntityViewImpl(Entity entity, EntityManager& manager)
			: mEntity(entity)
			, mManager(manager)
		{
		}

		template<typename ComponentType>
		ComponentType* addComponent()
		{
			return mManager.template addComponent<ComponentType>(mEntity);
		}

		template<typename ComponentType>
		void removeComponent()
		{
			mManager.template removeComponent<ComponentType>(mEntity);
		}

		template<typename... Components>
		std::tuple<Components*...> getComponents()
		{
			return mManager.template getEntityComponents<Components...>(mEntity);
		}

		template<typename ComponentType>
		ComponentType* scheduleAddComponent()
		{
			return mManager.template scheduleAddComponent<ComponentType>(mEntity);
		}

		template<typename ComponentType>
		void scheduleRemoveComponent()
		{
			mManager.template scheduleRemoveComponent<ComponentType>(mEntity);
		}

		[[nodiscard]] Entity getEntity() const
		{
			return mEntity;
		}

		EntityManager& getManager() { return mManager; }

	private:
		Entity mEntity;
		EntityManager& mManager;
	};

	template <typename ComponentTypeId>
	class AsyncEntityViewImpl
	{
	public:
		using AsyncEntityManager = AsyncEntityManagerImpl<ComponentTypeId>;

		AsyncEntityViewImpl(Entity entity, AsyncEntityManager& manager)
			: mEntity(entity)
			, mManager(manager)
		{
		}

		template<typename Operation>
		auto* addComponent(const Operation& operation)
		{
			return operation.TEMPLATE_MSVC_FIX addComponent(mManager, mEntity);
		}

		template<typename Operation>
		void removeComponent(const Operation& operation)
		{
			operation.template removeComponent(mManager, mEntity);
		}

		template<typename Operation>
		auto getComponents(const Operation& operation)
		{
			return operation.TEMPLATE_MSVC_FIX getEntityComponents(mManager, mEntity);
		}

		template<typename Operation>
		auto* scheduleAddComponent(const Operation& operation)
		{
			return operation.TEMPLATE_MSVC_FIX scheduleAddComponent(mManager, mEntity);
		}

		template<typename Operation>
		void scheduleRemoveComponent(const Operation& operation)
		{
			operation.TEMPLATE_MSVC_FIX scheduleRemoveComponent(mManager, mEntity);
		}

		[[nodiscard]] Entity getEntity() const
		{
			return mEntity;
		}

		AsyncEntityManager& getManager() { return mManager; }

	private:
		Entity mEntity;
		AsyncEntityManager& mManager;
	};

} // namespace RaccoonEcs
