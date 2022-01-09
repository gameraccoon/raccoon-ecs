#pragma once

#include "async_scheduled_operations.h"
#include "error_handling.h"

namespace RaccoonEcs
{
	/**
	 * The base class for game Systems that work with AsyncSystemManager
	 *
	 * Abstract
	 */
	template<typename ComponentTypeId, typename EntityManagerKeyType>
	class AsyncSystemBase
	{
	public:
		using OptionalScheduledOperations = OptionalScheduledOperationsImpl<ComponentTypeId, EntityManagerKeyType>;

	public:
		virtual ~AsyncSystemBase() = default;

		// only one update can be used at a time
		virtual OptionalScheduledOperations updateAndSchedule() = 0;
		virtual void update() = 0;

		virtual void initResources() {}
		virtual void shutdown() {}
	};

	template<typename ComponentTypeId, typename SystemManagerKeyType>
	class AsyncSystem : public AsyncSystemBase<ComponentTypeId, SystemManagerKeyType>
	{
	public:
		using Base = AsyncSystemBase<ComponentTypeId, SystemManagerKeyType>;

		// disable ability to override updateAndSchedule
		typename Base::OptionalScheduledOperations updateAndSchedule() final
		{
			RACCOON_ECS_ERROR("AsyncSystem was used for a system that implies having scheduled operations. Use AsyncSystemExtOp");
			return {};
		}
	};

	template<typename ComponentTypeId, typename EntityManagerKeyType>
	class AsyncSystemExtOp : public AsyncSystemBase<ComponentTypeId, EntityManagerKeyType>
	{
	public:
		using Base = AsyncSystemBase<ComponentTypeId, EntityManagerKeyType>;

		// disable ability to override update
		void update() final
		{
			RACCOON_ECS_ERROR("AsyncSystemExtOp was used for a system that implies having scheduled operations. Use AsyncSystem instead");
		}
	};

} // namespace RaccoonEcs
