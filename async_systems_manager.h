#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "async_operations.h"
#include "async_system.h"
#include "error_handling.h"
#include "thread_pool.h"
#include "system_dependencies.h"
#include "async_entity_manager.h"
#include "component_set_holder.h"

namespace RaccoonEcs
{
	/**
	 * Manager for async game systems
	 */
	template<typename ComponentTypeId, typename EntityManagerKey = void>
	class AsyncSystemsManager
	{
	public:
		using SystemT = AsyncSystemBase<ComponentTypeId, EntityManagerKey>;
		using ScheduledOperations = ScheduledOperationsImpl<ComponentTypeId, EntityManagerKey>;
		using OptionalScheduledOperations = OptionalScheduledOperationsImpl<ComponentTypeId, EntityManagerKey>;

	public:
		AsyncSystemsManager()
			: mOwnThreadPool(new ThreadPool())
			, mThreadPool(*mOwnThreadPool)
		{}

		/**
		 * @brief Initializes AsyncSystemsManager with external thread pool
		 * The external thread pool should always have longer live time that the AsyncSystemsManager object
		 */
		AsyncSystemsManager(ThreadPool& externalThreadPool)
			: mThreadPool(externalThreadPool)
		{}

		template <typename Fn>
		void setGetEntityManagerFn(Fn&& newFn)
		{
			mGetEntityManagerFn = std::forward<Fn>(newFn);
		}

		template <typename Fn>
		void setGetComponentSetHolderFn(Fn&& newFn)
		{
			mGetComponentSetHolder = std::forward<Fn>(newFn);
		}

		template <typename SystemType, typename... ComponentOperations, typename... Args>
		void registerSystem(
			SystemDependencies&& dependencies,
			Args&&... args)
		{
			auto result = mSystemIdxById.emplace(SystemType::GetSystemId(), mSystems.size());
			RACCOON_ECS_ASSERT(result.second, std::string("System registred twice: ") + SystemType::GetSystemId());

			mSystemDependenciesData.emplace_back(SystemType::GetSystemId(), std::move(dependencies));

			(registerComponentDependencies<SystemType, ComponentOperations>(mSystemDependenciesData.back()), ...);

#ifdef RACCOON_ECS_DEBUG_CHECKS_ENABLED
			checkDependenciesCompatibility(mSystemDependenciesData.back());
#endif // RACCOON_ECS_DEBUG_CHECKS_ENABLED

			mSystems.emplace_back(new SystemType(ComponentOperations()..., std::forward<Args>(args)...));
			mSystemIds.push_back(SystemType::GetSystemId());
		}

		void update()
		{
			std::unique_lock lock(mCurrentFrameDependenciesMutex);
			mCurrentFrameDependencies = std::make_unique<SystemDependencyTracer>(mDependencyGraph);

			while (mCurrentFrameDependencies->hasNotRunSystems())
			{
				mCurrentFrameDependenciesUpdated.wait(
					lock,
					[this]
					{
						return !mCurrentFrameDependencies->getNextSystemsToRun().empty() || !mCurrentFrameDependencies->hasNotRunSystems();
					}
				);
				trySpawnNewSystemTasks(lock);
			}
		}

		void initResources()
		{
			for (std::unique_ptr<SystemT>& system : mSystems)
			{
				system->initResources();
			}
		}

		void shutdown()
		{
			for (std::unique_ptr<SystemT>& system : mSystems)
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
		void init(size_t threadsCount = 0, const std::function<void(const InnerDataAccessor&)> initFunc = nullptr)
		{
			buildDependencyGraph();

			if (initFunc)
			{
				const InnerDataAccessor dataAccessor;
				initFunc(dataAccessor);
			}

			if (threadsCount > 0)
			{
				mThreadPool.spawnThreads(threadsCount);
			}
		}

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
		static void registerInnerDataAccessor(SystemDependencyInnerData& dependenciesData)
		{
			dependenciesData.exclusiveGlobalAccess = true;
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
				return false;
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

		void trySpawnNewSystemTasks(std::unique_lock<std::mutex>& lock)
		{
			if (!mScheduledOperations.empty())
			{
				if (!mCurrentFrameDependencies->hasSystemsRunning())
				{
					// no systems are running, this is the safe time to execute what is scheduled
					executeScheduledOperations();
				}
				else
				{
					// wait for systems to finish and execute scheduled ops before starting something new
					return;
				}
			}

			std::vector<size_t> systemsToRun = mCurrentFrameDependencies->getNextSystemsToRun();

			// schedule tasks that exceed one (that we run below)
			for (size_t i = 1; i < systemsToRun.size(); ++i)
			{
				const size_t systemIdx = systemsToRun[i];
				mCurrentFrameDependencies->runSystem(systemIdx);
				mThreadPool.executeTask(
					[this, systemIdx]
					{
						OptionalScheduledOperations scheduledOps;
						if (mSystemDependenciesData[systemIdx].needsSynchronizationAfter)
						{
							scheduledOps = mSystems[systemIdx]->updateAndSchedule();
						}
						else
						{
							mSystems[systemIdx]->update();
						}

						{
							std::unique_lock lock(mCurrentFrameDependenciesMutex);

							if (scheduledOps.has_value())
							{
								mScheduledOperations.push_back(std::move(*scheduledOps));
							}

							mCurrentFrameDependencies->finishSystem(systemIdx);
							trySpawnNewSystemTasks(lock);
						}

						mCurrentFrameDependenciesUpdated.notify_one();
						return std::any{};
					}, nullptr
				);
			}

			if (!systemsToRun.empty())
			{
				const size_t systemIdx = systemsToRun[0];
				mCurrentFrameDependencies->runSystem(systemIdx);

				if (mSystemDependenciesData[systemIdx].needsSynchronizationAfter)
				{
					lock.unlock();
					OptionalScheduledOperations scheduledOps = mSystems[systemIdx]->updateAndSchedule();
					lock.lock();

					if (scheduledOps.has_value())
					{
						mScheduledOperations.push_back(std::move(*scheduledOps));
					}
				}
				else
				{
					lock.unlock();
					mSystems[systemIdx]->update();
					lock.lock();
				}

				mCurrentFrameDependencies->finishSystem(systemIdx);

				trySpawnNewSystemTasks(lock);
			}
		}

#ifdef RACCOON_ECS_DEBUG_CHECKS_ENABLED
		void checkDependenciesCompatibility(SystemDependencyInnerData& dependencies)
		{
			if (dependencies.exclusiveGlobalAccess)
			{
				if (!dependencies.componentsToRead.empty()
					|| !dependencies.componentsToWrite.empty()
					|| dependencies.needsSynchronizationAfter)
				{
					// it doesn't make sense to have exclusive access and specific operations at the same time
					// it probably will work together but is likely a flaw in the logic
					RACCOON_ECS_ERROR("A system have exclusive access and other operations at the same time. It's better to use only exclusive access to avoid confusion");
				}
			}
		}
#endif // RACCOON_ECS_DEBUG_CHECKS_ENABLED

		void executeScheduledOperations()
		{
			for (ScheduledOperations& operations : mScheduledOperations)
			{
				for (auto& [key, entities] : operations.entitiesToAdd)
				{
					EntityManagerImpl<ComponentTypeId>& entityManager = mGetEntityManagerFn(key).mSingleThreadedManagerRef;
					for (Entity entity : entities)
					{
						entityManager.addExistingEntityUnsafe(entity);
					}
				}

				for (auto& [key, data] : operations.componentsToAdd)
				{
					EntityManagerImpl<ComponentTypeId>& entityManager = mGetEntityManagerFn(key).mSingleThreadedManagerRef;
					for (typename ScheduledOperations::ComponentAddData componentAddData : data)
					{
						entityManager.addComponent(componentAddData.entity, componentAddData.component, componentAddData.componentTypeId);
					}
				}

				for (auto& [key, data] : operations.componentsToRemove)
				{
					EntityManagerImpl<ComponentTypeId>& entityManager = mGetEntityManagerFn(key).mSingleThreadedManagerRef;
					for (typename ScheduledOperations::ComponentRemoveData componentRemoveData : data)
					{
						entityManager.removeComponent(componentRemoveData.entity, componentRemoveData.componentTypeId);
					}
				}

				for (auto& [key, data] : operations.entitiesToRemove)
				{
					EntityManagerImpl<ComponentTypeId>& entityManager = mGetEntityManagerFn(key).mSingleThreadedManagerRef;
					for (Entity entity : data)
					{
						entityManager.removeEntity(entity);
					}
				}
			}
			mScheduledOperations.clear();
		}

	private:
		// containers that are immutable after calling init()
		std::vector<std::unique_ptr<SystemT>> mSystems;
		std::vector<std::string> mSystemIds;
		std::vector<SystemDependencyInnerData> mSystemDependenciesData;
		std::unordered_map<std::string, size_t> mSystemIdxById;
		DependencyGraph mDependencyGraph;
		std::unique_ptr<ThreadPool> mOwnThreadPool;
		ThreadPool& mThreadPool;
		std::function<AsyncEntityManagerImpl<ComponentTypeId>&(const EntityManagerKey&)> mGetEntityManagerFn;
		std::function<ComponentSetHolderImpl<ComponentTypeId>&(const EntityManagerKey&)> mGetComponentSetHolder;

		// mutable data
		std::unique_ptr<SystemDependencyTracer> mCurrentFrameDependencies;
		std::mutex mCurrentFrameDependenciesMutex;
		std::condition_variable mCurrentFrameDependenciesUpdated;
		std::vector<ScheduledOperations> mScheduledOperations;
	};

} // namespace RaccoonEcs
