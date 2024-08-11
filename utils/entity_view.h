#pragma once

#include "../entity.h"
#include "../msvc_fix.h"

namespace RaccoonEcs
{
	/**
	 * @brief Non-owning wrapper around entity and its current entity manager
	 */
	template<typename EntityManager>
	class EntityViewImpl
	{
	public:
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

		template<typename Component>
		bool hasComponent()
		{
			return mManager.template doesEntityHaveComponent<Component>(mEntity);
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
} // namespace RaccoonEcs
