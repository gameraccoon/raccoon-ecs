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
#include "thread_pool.h"

#include "system.h"

namespace RaccoonEcs
{
	struct SystemDependencies
	{
		explicit SystemDependencies(int customOrder = -1)
			: customOrder(customOrder)
		{}

		template<typename... Systems>
		SystemDependencies&& goesAfter() &&
		{
			(systemsBefore.push_back(Systems::GetSystemId()), ...);
			return std::move(*this);
		}

		template<typename... Systems>
		SystemDependencies&& goesBefore() &&
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
	void pushBackUnique(V& inOutVector, T&& value)
	{
		if (std::find(inOutVector.begin(), inOutVector.end(), value) == inOutVector.end())
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

		void addDependency(size_t beforeIdx, size_t afterIdx)
		{
			pushBackUnique(mNodes[afterIdx].nodesBefore, beforeIdx);
			pushBackUnique(mNodes[beforeIdx].nodesAfter, afterIdx);
		}

		void addIncompatibility(size_t firstSystemIdx, size_t secondSystemIdx)
		{
			if (firstSystemIdx < secondSystemIdx)
			{
				mIncompatibilities.insert(std::make_pair(firstSystemIdx, secondSystemIdx));
			}
			else
			{
				mIncompatibilities.insert(std::make_pair(secondSystemIdx, firstSystemIdx));
			}
		}

		void finalize()
		{
			for (size_t nodeIdx = 0; nodeIdx < mNodes.size(); ++nodeIdx)
			{
				Node& node = mNodes[nodeIdx];
				// calculate distanceToTheLast
				if (node.nodesAfter.empty())
				{
					std::vector<size_t> nextNodes;
					node.distanceToTheLast = 1;
					nextNodes.push_back(nodeIdx);

					while (!nextNodes.empty())
					{
						Node& currentNode = mNodes[nextNodes.back()];
						nextNodes.pop_back();

						for (size_t nodeBeforeIdx : currentNode.nodesBefore)
						{
							Node& nodeBefore = mNodes[nodeBeforeIdx];
							nodeBefore.distanceToTheLast = std::min(currentNode.distanceToTheLast + 1, nodeBefore.distanceToTheLast);
							nextNodes.push_back(nodeBeforeIdx);
						}
					}
				}

				// populate mFirstNodes
				if (mNodes[nodeIdx].nodesBefore.empty())
				{
					mFirstNodes.push_back(nodeIdx);
				}
			}
		}

		bool areSystemsCompatible(size_t firstSystemIdx, size_t secondSystemIdx) const
		{
			if (firstSystemIdx < secondSystemIdx)
			{
				return mIncompatibilities.find(std::make_pair(firstSystemIdx, secondSystemIdx)) == mIncompatibilities.end();
			}
			else
			{
				return mIncompatibilities.find(std::make_pair(secondSystemIdx, firstSystemIdx)) == mIncompatibilities.end();
			}
		}

	private:
		struct Node
		{
			std::vector<size_t> nodesBefore;
			// to faster trace the graph
			std::vector<size_t> nodesAfter;
			// how many other systems depend on it (including inderect dependencies)
			size_t distanceToTheLast = std::numeric_limits<size_t>::max();
		};

		struct IndexPairHash
		{
			std::size_t operator() (std::pair<size_t, size_t> const &pair) const
			{
				return pair.first ^ std::rotl(pair.second, 7);
			}
		};

		std::vector<Node> mNodes;
		std::vector<size_t> mFirstNodes;
		std::unordered_set<std::pair<size_t, size_t>, IndexPairHash> mIncompatibilities;
	};

	class SystemDependencyTracer
	{
	public:
		SystemDependencyTracer(const DependencyGraph& dependencyGraph)
			: mDependencyGraph(&dependencyGraph)
			, mResolvedDependencies(mDependencyGraph->mNodes.size(), false)
			, mNextSystems(mDependencyGraph->mFirstNodes)
		{
		}

		void finishSystem(size_t finishedSystem)
		{
			mActiveSystems.erase(
				std::remove(
					mActiveSystems.begin(),
					mActiveSystems.end(),
					finishedSystem),
				mActiveSystems.end());

			mResolvedDependencies[finishedSystem] = true;

			const DependencyGraph::Node& systemNode = mDependencyGraph->mNodes[finishedSystem];
			for (size_t nodeAfter : systemNode.nodesAfter)
			{
				pushBackUnique(mNextSystems, nodeAfter);
			}
		}

		std::vector<size_t> getNextSystemsToRun() const
		{
			std::vector<size_t> systemsToRun;

			if (!mNextSystems.empty())
			{
				// O(n*m)
				for (size_t nextSystem : mNextSystems)
				{
					if (canRunSystem(nextSystem))
					{
						systemsToRun.push_back(nextSystem);
					}
				}
			}

			filterIncompatibleSystems(systemsToRun);

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

		bool canRunSystem(size_t systemIdx) const
		{
			const std::vector<size_t>& nodesBefore = mDependencyGraph->mNodes[systemIdx].nodesBefore;

			for (size_t nodeBeforeIdx : nodesBefore)
			{
				if (mResolvedDependencies[nodeBeforeIdx] == false)
				{
					return false;
				}
			}

			for (size_t activeSystem : mActiveSystems)
			{
				if (!mDependencyGraph->areSystemsCompatible(systemIdx, activeSystem))
				{
					return false;
				}
			}

			return true;
		}

		void filterIncompatibleSystems(std::vector<size_t>& systems) const
		{
			std::vector<size_t> systemsToExclude;

			// O(n^2)
			for (size_t i = 0; i + 1 < systems.size(); ++i)
			{
				for (size_t j = i + 1; j < systems.size(); ++j)
				{
					if (!mDependencyGraph->areSystemsCompatible(systems[i], systems[j]))
					{
						// we prefer to keep systems that are more distant from the last
						if (mDependencyGraph->mNodes[systems[i]].distanceToTheLast
							<
							mDependencyGraph->mNodes[systems[j]].distanceToTheLast)
						{
							systemsToExclude.push_back(i);
						}
						else
						{
							systemsToExclude.push_back(j);
						}
					}
				}
			}

			// O(n log n)
			std::sort(systemsToExclude.begin(), systemsToExclude.end(), std::greater());
			systemsToExclude.erase(std::unique(systemsToExclude.begin(), systemsToExclude.end()), systemsToExclude.end());
			for (size_t idx : systemsToExclude)
			{
				std::swap(systems[idx], systems.back());
			}
			systems.resize(systems.size() - systemsToExclude.size());
		}

	private:
		const DependencyGraph* mDependencyGraph;
		std::vector<bool> mResolvedDependencies;
		std::vector<size_t> mActiveSystems;
		std::vector<size_t> mNextSystems;
	};

	struct AsyncSystemsFrameTime
	{
		struct OneSystemTime
		{
			size_t systemIdx;
			size_t workerThreadId;
			std::chrono::time_point<std::chrono::system_clock> start;
			std::chrono::time_point<std::chrono::system_clock> end;
		};

		std::chrono::microseconds frameTime;
		std::vector<OneSystemTime> systemsTime;
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
#ifdef RACCOON_ECS_PROFILE_SYSTEMS
			std::chrono::time_point<std::chrono::system_clock> frameStart = std::chrono::system_clock::now();
			mThisFrameTime.systemsTime.clear();
#endif // RACCOON_ECS_PROFILE_SYSTEMS

			mCurrentFrameDependencies = std::make_unique<SystemDependencyTracer>(mDependencyGraph);

			trySpawnNewSystemTasks();

			// this will block the thread and spawn new tasks as needed
			mThreadPool.finalizeTasks();

#ifdef RACCOON_ECS_PROFILE_SYSTEMS
				std::chrono::time_point<std::chrono::system_clock> frameEnd = std::chrono::system_clock::now();
				mThisFrameTime.frameTime = std::chrono::duration_cast<std::chrono::microseconds>(frameEnd - frameStart);
				mPreviousFrameTime = mThisFrameTime;
#endif // RACCOON_ECS_PROFILE_SYSTEMS
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
		void init(size_t threadsCount, const std::function<void(const InnerDataAccessor&)> initFunc = nullptr)
		{
			buildDependencyGraph();

			if (initFunc)
			{
				const InnerDataAccessor dataAccessor;
				initFunc(dataAccessor);
			}

			mThreadPool.spawnThreads(threadsCount);
		}

#ifdef RACCOON_ECS_PROFILE_SYSTEMS
		AsyncSystemsFrameTime getPreviousFrameTimeData()
		{
			return mPreviousFrameTime;
		}
#endif // RACCOON_ECS_PROFILE_SYSTEMS

		const std::vector<std::string>& getSystemNames()
		{
			return mSystemIds;
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
				pushBackUnique(dependenciesData.componentsToRead, Component::GetTypeId());
			}
			else
			{
				pushBackUnique(dependenciesData.componentsToWrite, Component::GetTypeId());
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
			pushBackUnique(dependenciesData.componentsToWrite, Component::GetTypeId());
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
			mDependencyGraph.initNodes(mSystems.size());

			for (size_t systemIdx = 0; systemIdx < mSystems.size(); ++systemIdx)
			{
				const SystemDependencyInnerData& dependencyData = mSystemDependenciesData[systemIdx];
				for (const std::string& systemBefore : dependencyData.explicitDependencies.systemsBefore)
				{
					size_t systemBeforeIdx = mSystemIdxById.at(systemBefore);
					mDependencyGraph.addDependency(systemBeforeIdx, systemIdx);
				}
				for (const std::string& systemAfter : dependencyData.explicitDependencies.systemsAfter)
				{
					size_t systemAfterIdx = mSystemIdxById.at(systemAfter);
					mDependencyGraph.addDependency(systemIdx, systemAfterIdx);
				}
			}

			for (size_t i = 0; i + 1 < mSystems.size(); ++i)
			{
				for (size_t j = i + 1; j < mSystems.size(); ++j)
				{
					if (!areSystemsCompatible(i, j))
					{
						mDependencyGraph.addIncompatibility(i, j);
					}
				}
			}

			mDependencyGraph.finalize();
		}

		bool areSystemsCompatible(size_t firstSystemIdx, size_t secondSystemIdx)
		{
			const SystemDependencyInnerData& firstSystemDependencies = mSystemDependenciesData[firstSystemIdx];
			const SystemDependencyInnerData& secondSystemDependencies = mSystemDependenciesData[secondSystemIdx];

			// sych systems will be run exclusively anyway, so no need to add incompatibility specifically
			if (firstSystemDependencies.exclusiveGlobalAccess || secondSystemDependencies.exclusiveGlobalAccess)
			{
				return true;
			}

			auto doesContain = [](const std::vector<ComponentTypeId>& vector, ComponentTypeId value) -> bool {
				return (std::find(vector.begin(), vector.end(), value) != vector.end());
			};

			for (ComponentTypeId writeComponent : firstSystemDependencies.componentsToWrite)
			{
				if (doesContain(firstSystemDependencies.componentsToRead, writeComponent))
				{
					return false;
				}

				if (doesContain(firstSystemDependencies.componentsToWrite, writeComponent))
				{
					return false;
				}
			}

			for (ComponentTypeId readComponent : firstSystemDependencies.componentsToRead)
			{
				if (doesContain(firstSystemDependencies.componentsToWrite, readComponent))
				{
					return false;
				}
			}

			return true;
		}

		void trySpawnNewSystemTasks()
		{
			std::vector<size_t> systemsToRun = mCurrentFrameDependencies->getNextSystemsToRun();

			for (size_t systemIdx : systemsToRun)
			{
				mCurrentFrameDependencies->runSystem(systemIdx);
				mThreadPool.executeTask(
					[systemIdx, system = mSystems[systemIdx].get()]
					{
#ifdef RACCOON_ECS_PROFILE_SYSTEMS
						AsyncSystemsFrameTime::OneSystemTime time;
						time.workerThreadId = ThreadPool::GetThisThreadId();
						time.systemIdx = systemIdx;
						time.start = std::chrono::system_clock::now();
#endif // RACCOON_ECS_PROFILE_SYSTEMS

						// real work happening here
						system->update();

#ifdef RACCOON_ECS_PROFILE_SYSTEMS
						time.end = std::chrono::system_clock::now();
						return time;
#else
						return std::any{};
#endif // RACCOON_ECS_PROFILE_SYSTEMS
					},
					[this, systemIdx]([[maybe_unused]] std::any&& result)
					{
#ifdef RACCOON_ECS_PROFILE_SYSTEMS
						mThisFrameTime.systemsTime.push_back(std::any_cast<AsyncSystemsFrameTime::OneSystemTime>(result));
#endif // RACCOON_ECS_PROFILE_SYSTEMS

						mCurrentFrameDependencies->finishSystem(systemIdx);
						trySpawnNewSystemTasks();
					}
				);
			}
		}

	private:
		std::vector<std::unique_ptr<System>> mSystems;
		std::vector<std::string> mSystemIds;
		std::vector<SystemDependencyInnerData> mSystemDependenciesData;
		std::unordered_map<std::string, size_t> mSystemIdxById;
		DependencyGraph mDependencyGraph;
		ThreadPool mThreadPool;
		std::unique_ptr<SystemDependencyTracer> mCurrentFrameDependencies;

#ifdef RACCOON_ECS_PROFILE_SYSTEMS
		AsyncSystemsFrameTime mThisFrameTime;
		AsyncSystemsFrameTime mPreviousFrameTime;
#endif // RACCOON_ECS_PROFILE_SYSTEMS
	};

} // namespace RaccoonEcs
