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
		friend class BaseAsyncOperation;

	public:
		using EntityManager = EntityManagerImpl<ComponentTypeId>;

	public:
		AsyncEntityManagerImpl(EntityManager& singleThreadedManagerRef)
			: mSingleThreadedManagerRef(singleThreadedManagerRef)
		{}

	private:
		// can be accessed only from BaseAsyncOperation class
		EntityManager& mSingleThreadedManagerRef;
	};

} // namespace RaccoonEcs
