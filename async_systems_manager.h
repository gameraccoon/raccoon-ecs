#pragma once

#include <chrono>
#include <memory>
#include <vector>
#include <type_traits>
#include <functional>

#include <iostream>

#include "async_operations.h"
#include "error_handling.h"

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
			(systemsBefore.push_back(Systems::GetSystemId()), ...);
			return *this;
		}

		template<typename... Systems>
		SystemDependencies& isDependencyFor()
		{
			(systemsAfter.push_back(Systems::GetSystemId()), ...);
			return *this;
		}

		template<typename... Systems>
		SystemDependencies& canNotBeRunTogetherWith()
		{
			(incompatibleSystems.push_back(Systems::GetSystemId()), ...);
			return *this;
		}

		template<typename... Systems>
		SystemDependencies& limitConcurrentlyRunSystemsTo(int systemsCount)
		{
			allowConcurrentSystemsCount = systemsCount;
			return *this;
		}

		std::vector<std::string> systemsBefore;
		std::vector<std::string> systemsAfter;
		std::vector<std::string> incompatibleSystems;
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
	template<typename ComponentTypeId>
	class AsyncSystemsManager
	{
	public:
		template <typename SystemType, typename... ComponentOperations, typename... Args>
		void registerSystem(
			[[maybe_unused]] SystemDependencies dependencies = SystemDependencies(),
			Args&&... args)
		{
			auto result = mSystemIdxByName.emplace(SystemType::GetSystemId(), mSystems.size());
			RACCOON_ECS_ASSERT(result.second, std::string("System registred twice: ") + SystemType::GetSystemId());

			mSystemDependenciesData.emplace_back();

			(registerComponentDependencies<SystemType, ComponentOperations>(mSystemDependenciesData.back()), ...);

			mSystems.emplace_back(new SystemType(ComponentOperations()..., std::forward<Args>(args)...));
			mSystemIds.push_back(SystemType::GetSystemId());
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

		/**
		 * @brief Initializes functions graph and optionally calls initFunc that can be used to init data
		 * @param initFunc
		 *
		 * Call it once after registering all your systems and before calling update for the first time
		 */
		void init(const std::function<void(const InnerDataAccessor&)> initFunc = nullptr)
		{
			buildSystemsGraph();

			if (initFunc)
			{
				const InnerDataAccessor dataAccessor;
				initFunc(dataAccessor);
			}
		}

	private:
		struct SystemDependencyInnerData
		{
			SystemDependencies explicitDependencies;
			std::vector<ComponentTypeId> componentsToRead;
			std::vector<ComponentTypeId> componentsToWrite;
			bool needsSynchronizationAfter = false;
			bool filtersEntities = false;
			bool exclusiveGlobalAccess = false;
		};

	private:
		template<typename SystemType, typename Component>
		static void registerFilteredComponent(SystemDependencyInnerData& dependenciesData)
		{
			if (std::is_const_v<Component>)
			{
				dependenciesData.componentsToRead.push_back(Component::GetTypeId());
			}
			else
			{
				dependenciesData.componentsToWrite.push_back(Component::GetTypeId());
			}
		}

		template<typename SystemType, typename... Components>
		static void registerComponentFilter(const ComponentFilter<Components...>&, SystemDependencyInnerData& dependenciesData)
		{
			(registerFilteredComponent<SystemType, Components>(dependenciesData), ...);
		}

		template<typename SystemType, typename Component>
		static void registerComponentAdder(const ComponentAdder<Component>&, SystemDependencyInnerData& dependenciesData)
		{
			dependenciesData.componentsToWrite.push_back(Component::GetTypeId());
			dependenciesData.needsSynchronizationAfter = true;
		}

		template<typename SystemType, typename Component>
		static void registerComponentRemover(const ComponentRemover<Component>&, SystemDependencyInnerData& dependenciesData)
		{
			dependenciesData.needsSynchronizationAfter = true;
		}

		template<typename SystemType, typename Component>
		static void registerComponentToSelectEntity(SystemDependencyInnerData& /*dependenciesData*/)
		{
			// nothing to do here, selecting entities doesn't affect components
		}

		template<typename SystemType, typename... Components>
		static void registerEntitySelector(const EntitySelector<Components...>&, SystemDependencyInnerData& dependenciesData)
		{
			(registerComponentToSelectEntity<SystemType, Components>(dependenciesData), ...);
		}

		template<typename SystemType>
		static void registerEntityAdder(SystemDependencyInnerData& dependenciesData)
		{
			dependenciesData.needsSynchronizationAfter = true;
		}

		template<typename SystemType>
		static void registerEntityRemover(SystemDependencyInnerData& dependenciesData)
		{
			dependenciesData.needsSynchronizationAfter = true;
		}

		template<typename SystemType>
		static void registerEntityTransferer(SystemDependencyInnerData& dependenciesData)
		{
			dependenciesData.needsSynchronizationAfter = true;
		}

		template<typename SystemType>
		static void registerScheduledActionsExecutor(SystemDependencyInnerData& dependenciesData)
		{
			dependenciesData.exclusiveGlobalAccess = true;
		}

		template<typename SystemType>
		static void registerInnerDataAccessor(SystemDependencyInnerData& dependenciesData)
		{
			dependenciesData.exclusiveGlobalAccess = true;
			dependenciesData.needsSynchronizationAfter = true;
		}

		template<template<typename...> class Template, typename... Type>
		struct IsBaseOfInstantiation : std::false_type { };

		template<template<typename...> class Template, typename... Type>
		struct IsBaseOfInstantiation<Template, Template<Type...>> : std::true_type { };

		template<typename SystemType, typename AsyncOperation>
		void registerComponentDependencies(SystemDependencyInnerData& dependenciesData)
		{
			if constexpr (IsBaseOfInstantiation<ComponentFilter, AsyncOperation>::value)
			{
				registerComponentFilter<SystemType>(AsyncOperation{}, dependenciesData);
			}
			else if constexpr (IsBaseOfInstantiation<ComponentAdder, AsyncOperation>::value)
			{
				registerComponentAdder<SystemType>(AsyncOperation{}, dependenciesData);
			}
			else if constexpr (IsBaseOfInstantiation<ComponentRemover, AsyncOperation>::value)
			{
				registerComponentRemover<SystemType>(AsyncOperation{}, dependenciesData);
			}
			else if constexpr (IsBaseOfInstantiation<EntitySelector, AsyncOperation>::value)
			{
				registerEntitySelector<SystemType>(AsyncOperation{}, dependenciesData);
			}
			else if constexpr (std::is_same<EntityAdder, AsyncOperation>::value)
			{
				registerEntityAdder<SystemType>(dependenciesData);
			}
			else if constexpr (std::is_same<EntityRemover, AsyncOperation>::value)
			{
				registerEntityRemover<SystemType>(dependenciesData);
			}
			else if constexpr (std::is_same<EntityTransferer, AsyncOperation>::value)
			{
				registerEntityTransferer<SystemType>(dependenciesData);
			}
			else if constexpr (std::is_same<ScheduledActionsExecutor, AsyncOperation>::value)
			{
				registerScheduledActionsExecutor<SystemType>(dependenciesData);
			}
			else if constexpr (std::is_same<InnerDataAccessor, AsyncOperation>::value)
			{
				registerInnerDataAccessor<SystemType>(dependenciesData);
			}
			else
			{
				[]<bool flag = false>(){static_assert(flag, "Unknown filter type");}();
			}
		}

		class DependencyGraph
		{
		public:
			void addNode(const std::string& id)
			{
				mIdToNode.emplace(id, mNodes.size());
				mNodes.emplace_back(id);
			}

			void addDependency(const std::string& before, const std::string& after)
			{
				size_t idBefore = mIdToNode.at(before);
				size_t idAfter = mIdToNode.at(after);

				mNodes[idBefore].nodeDependencies.push_back(idAfter);
			}

		private:
			struct Node
			{
				Node(const std::string& name) : systemName(name) {}

				std::string systemName;
				std::vector<size_t> nodeDependencies;
			};

			std::unordered_map<std::string, size_t> mIdToNode;
			std::vector<Node> mNodes;
		};

		void buildSystemsGraph()
		{
			DependencyGraph graph;
			for (const std::string& systemId : mSystemIds)
			{
				graph.addNode(systemId);
			}
		}

	private:
		std::vector<std::unique_ptr<System>> mSystems;
		std::vector<std::string> mSystemIds;
		std::vector<SystemDependencyInnerData> mSystemDependenciesData;
		std::unordered_map<std::string, size_t> mSystemIdxByName;
	};

} // namespace RaccoonEcs
