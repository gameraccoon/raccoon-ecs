#pragma once

#include <chrono>
#include <memory>
#include <vector>
#include <type_traits>

#include <iostream>

#include "async_operations.h"

#include "system.h"

namespace RaccoonEcs
{
	struct SystemDependencies
	{
		explicit SystemDependencies(int customOrder = -1)
			: customOrder(customOrder)
		{}

		template<typename... Systems>
		SystemDependencies& dependsOn()
		{
			return *this;
		}

		template<typename... Systems>
		SystemDependencies& isDependencyFor()
		{
			return *this;
		}

		template<typename... Systems>
		SystemDependencies& canNotBeRunTogetherWith()
		{
			return *this;
		}

		template<typename... Systems>
		SystemDependencies& limitConcurrentlyRunSystemsTo(int systemsCount)
		{
			allowConcurrentSystemsCount = systemsCount;
			return *this;
		}

		std::vector<int> systemsBefore;
		std::vector<int> systemsAfter;
		std::vector<int> incompatibleSystems;
		int allowConcurrentSystemsCount = -1;
		int customOrder;
	};

	class SystemDependencyGraph
	{
	public:
	private:
		struct Node
		{
			int id;
			SystemDependencies explicitDependencies;
			std::shared_ptr<struct SynchronizationPoint> nextSyncPoint;
		};

		struct SynchronizationPoint
		{
			std::vector<int> previousNodes;
			std::vector<std::shared_ptr<Node>> nextNodes;
		};

	private:
		std::shared_ptr<SynchronizationPoint> mStartPoint;
	};

	/**
	 * Manager for async game systems
	 */
	class AsyncSystemsManager
	{
	public:
		template <typename SystemType, typename... ComponentOperations, typename... Args>
		void registerSystem(
			[[maybe_unused]] SystemDependencies dependencies = SystemDependencies(),
			Args&&... args)
		{
			(registerComponentDependencies<SystemType, ComponentOperations>(), ...);

			mSystems.emplace_back(new SystemType(ComponentOperations()..., std::forward<Args>(args)...));
		}

		void update()
		{
			for (std::unique_ptr<System>& system : mSystems)
			{
				// real work is being done here
				system->update();
			}
		}

		void initResources()
		{
			for (std::unique_ptr<System>& system : mSystems)
			{
				system->initResources();
			}
		}

		void shutdown()
		{
			for (std::unique_ptr<System>& system : mSystems)
			{
				system->shutdown();
			}
			mSystems.clear();
		}

		template<typename Func>
		void preStartInit(Func&& func)
		{
			const InnerDataAccessor dataAccessor;
			std::forward<Func>(func)(dataAccessor);
		}

	private:
		template<typename SystemType, typename Component>
		static void registerFilteredComponent()
		{
			if (std::is_const_v<Component>)
			{
				// read access
			}
			else
			{
				// write access
			}
		}

		template<typename SystemType, typename... Components>
		static void registerComponentFilter(const ComponentFilter<Components...>&)
		{
			(registerFilteredComponent<SystemType, Components>(), ...);
		}

		template<template<typename...> class Template, typename... Type>
		struct IsBaseOfInstantiation : std::false_type { };

		template<template<typename...> class Template, typename... Type>
		struct IsBaseOfInstantiation<Template, Template<Type...>> : std::true_type { };

		template<typename SystemType, typename AsyncOperation>
		void registerComponentDependencies()
		{
			if constexpr (IsBaseOfInstantiation<ComponentFilter, AsyncOperation>::value)
			{
				registerComponentFilter<SystemType>(AsyncOperation{});
			}
			else if constexpr (IsBaseOfInstantiation<ComponentAdder, AsyncOperation>::value)
			{

			}
		}

	private:
		std::vector<std::unique_ptr<System>> mSystems;
	};

} // namespace RaccoonEcs
