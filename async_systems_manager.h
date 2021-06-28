#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <numeric>
#include <type_traits>
#include <unordered_set>
#include <vector>

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
		SystemDependencies&& dependsOn() &&
		{
			(systemsBefore.push_back(Systems::GetSystemId()), ...);
			return std::move(*this);
		}

		template<typename... Systems>
		SystemDependencies&& isDependencyFor() &&
		{
			(systemsAfter.push_back(Systems::GetSystemId()), ...);
			return std::move(*this);
		}

		template<typename... Systems>
		SystemDependencies&& canNotBeRunTogetherWith() &&
		{
			(incompatibleSystems.push_back(Systems::GetSystemId()), ...);
			return std::move(*this);
		}

		template<typename... Systems>
		SystemDependencies&& limitConcurrentlyRunSystemsTo(int systemsCount) &&
		{
			allowConcurrentSystemsCount = systemsCount;
			return std::move(*this);
		}

		std::vector<std::string> systemsBefore;
		std::vector<std::string> systemsAfter;
		std::vector<std::string> incompatibleSystems;
		int allowConcurrentSystemsCount = -1;
		int customOrder;
	};

	template<typename V, typename T>
	void pushUniqueValueToVector(V& inOutVector, T&& value)
	{
		if (std::find(inOutVector.begin(), inOutVector.end(), value) != inOutVector.end())
		{
			inOutVector.push_back(std::forward<T>(value));
		}
	}

	class DependencyGraph
	{
		friend class SystemDependencyTracer;

	public:
		void initNodes(size_t count)
		{
			mNodes.resize(count);
		}

		void addDependency(size_t before, size_t after)
		{
			pushUniqueValueToVector(mNodes[before].nodeDependencies, after);
			pushUniqueValueToVector(mNodes[after].dependentNodes, before);
		}

		void finalize()
		{
			for (size_t nodeIdx = 0; nodeIdx < mNodes.size(); ++nodeIdx)
			{
				Node& node = mNodes[nodeIdx];
				// calculate distanceToTheLast
				if (node.nodeDependencies.empty())
				{
					std::vector<size_t> nextNodes;
					node.distanceToTheLast = 1;
					nextNodes.push_back(nodeIdx);

					while (!nextNodes.empty())
					{
						Node& currentNode = mNodes[nextNodes.back()];
						nextNodes.pop_back();

						for (size_t dependentNodeIdx : currentNode.dependentNodes)
						{
							Node& dependentNode = mNodes[dependentNodeIdx];
							dependentNode.distanceToTheLast = std::min(currentNode.distanceToTheLast + 1, dependentNode.distanceToTheLast);
							nextNodes.push_back(dependentNodeIdx);
						}
					}
				}

				// populate mFirstNodes
				if (mNodes[nodeIdx].nodeDependencies.empty())
				{
					mFirstNodes.push_back(nodeIdx);
				}
			}
		}

	private:
		struct Node
		{
			std::vector<size_t> nodeDependencies;
			// to faster trace the graph
			std::vector<size_t> dependentNodes;
			// how many other systems depend on it (including inderect dependencies)
			size_t distanceToTheLast = std::numeric_limits<size_t>::max();
		};

		std::vector<Node> mNodes;
		std::vector<size_t> mFirstNodes;
	};

	class SystemDependencyTracer
	{
	public:
		SystemDependencyTracer(const DependencyGraph& dependencyGraph)
			: mDependencyGraph(dependencyGraph)
			, mResolvedDependencies(mDependencyGraph.mNodes.size(), false)
			, mNextSystems(mDependencyGraph.mFirstNodes)
		{
		}

		std::vector<size_t> finishSystemAndGetNextSystemsToRun(size_t finishedSystem)
		{
			mActiveSystems.erase(
				std::remove(
					mActiveSystems.begin(),
					mActiveSystems.end(),
					finishedSystem),
				mActiveSystems.end());

			mResolvedDependencies[finishedSystem] = true;

			const DependencyGraph::Node& systemNode = mDependencyGraph.mNodes[finishedSystem];
			for (size_t dependentNode : systemNode.dependentNodes)
			{
				pushUniqueValueToVector(mNextSystems, dependentNode);
			}

			std::vector<size_t> systemsToRun;

			if (!mNextSystems.empty())
			{
				for (size_t nextSystem : mNextSystems)
				{
					if (canRunSystem(nextSystem))
					{
						systemsToRun.push_back(nextSystem);
					}
				}
			}

			return systemsToRun;
		}

		void runSystem(size_t systemIdx)
		{
			mNextSystems.erase(
				std::remove(
					mNextSystems.begin(),
					mNextSystems.end(),
					systemIdx),
				mNextSystems.end()
			);

			mActiveSystems.push_back(systemIdx);
		}

		bool canRunSystem(size_t systemIdx)
		{
			const std::vector<size_t>& dependencies = mDependencyGraph.mNodes[systemIdx].nodeDependencies;

			for (size_t depedency : dependencies)
			{
				if (mResolvedDependencies[depedency] == false)
				{
					return false;
				}
			}

			return true;
		}

	private:
		const DependencyGraph& mDependencyGraph;
		std::vector<bool> mResolvedDependencies;
		std::vector<size_t> mActiveSystems;
		std::vector<size_t> mNextSystems;
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
			SystemDependencies&& dependencies,
			Args&&... args)
		{
			auto result = mSystemIdxById.emplace(SystemType::GetSystemId(), mSystems.size());
			RACCOON_ECS_ASSERT(result.second, std::string("System registred twice: ") + SystemType::GetSystemId());

			mSystemDependenciesData.emplace_back(SystemType::GetSystemId(), std::move(dependencies));

			(registerComponentDependencies<SystemType, ComponentOperations>(mSystemDependenciesData.back()), ...);

			mSystems.emplace_back(new SystemType(ComponentOperations()..., std::forward<Args>(args)...));
			mSystemIds.push_back(SystemType::GetSystemId());
		}

		void update()
		{
			SystemDependencyTracer dependencies(mDependenctyGraph);

			//while (true)
			{
				//dependencies.finishSystemAndGetNextSystemsToRun();
			}

			for (auto& system : mSystems)
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
			buildDependencyGraph();

			if (initFunc)
			{
				const InnerDataAccessor dataAccessor;
				initFunc(dataAccessor);
			}
		}

	private:
		struct SystemDependencyInnerData
		{
			SystemDependencyInnerData(const std::string& systemId, SystemDependencies&& explicitDependencies)
				: systemId(systemId)
				, explicitDependencies(std::move(explicitDependencies))
			{}

			std::string systemId;
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

		void buildDependencyGraph()
		{
			mDependenctyGraph.initNodes(mSystems.size());

			for (size_t systemIdx = 0; systemIdx < mSystems.size(); ++systemIdx)
			{
				const SystemDependencyInnerData& dependencyData = mSystemDependenciesData[systemIdx];
				for (const std::string& systemBefore : dependencyData.explicitDependencies.systemsBefore)
				{
					size_t systemBeforeIdx = mSystemIdxById.at(systemBefore);
					mDependenctyGraph.addDependency(systemBeforeIdx, systemIdx);
				}
			}
		}

	private:
		std::vector<std::unique_ptr<System>> mSystems;
		std::vector<std::string> mSystemIds;
		std::vector<SystemDependencyInnerData> mSystemDependenciesData;
		std::unordered_map<std::string, size_t> mSystemIdxById;
		DependencyGraph mDependenctyGraph;
	};

} // namespace RaccoonEcs
