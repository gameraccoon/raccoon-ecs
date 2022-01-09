#pragma once

#include "entity.h"
#include "async_entity_manager.h"
#include "msvc_fix.h"

namespace RaccoonEcs
{
	/**
	 * @brief Non-owning wrapper around entity and its current async entity manager
	 */
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

		template<typename Operation, typename ComponentAddDataVector>
		auto* addComponent(const Operation& operation, ComponentAddDataVector& data)
		{
			return operation.TEMPLATE_MSVC_FIX addComponent(mManager, data, mEntity);
		}

		template<typename Operation>
		auto getComponents(const Operation& operation)
		{
			return operation.TEMPLATE_MSVC_FIX getEntityComponents(mManager, mEntity);
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
