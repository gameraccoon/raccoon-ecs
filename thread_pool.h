#pragma once

#include <any>
#include <condition_variable>
#include <functional>
#include <thread>
#include <vector>

#include "external/concurrentqueue/concurrentqueue.h"

#include "error_handling.h"

namespace RaccoonEcs
{
	class ThreadPool
	{
	public:
		template<typename Func = std::nullptr_t>
		ThreadPool(size_t threadsCount = 0, Func&& threadPreShutdownTask = nullptr)
			: mThreadPreShutdownTask(std::forward<Func>(threadPreShutdownTask))
		{
			spawnThreads(threadsCount);
		}

		~ThreadPool()
		{
			shutdown();
		}

		void shutdown()
		{
			mReadyToShutdown.store(true, std::memory_order::release);

			for (auto& thread : mThreads)
			{
				thread.join();
			}
			mThreads.clear();
		}

		void spawnThreads(size_t threadsCount, size_t firstThreadIndex = 1)
		{
			for (size_t i = 0; i < threadsCount; ++i)
			{
				const size_t threadId = mThreads.size() + firstThreadIndex;
				mThreads.emplace_back([this, threadId]
				{
					ThisThreadId = threadId;
					workerThreadFunction();
				});
			}
		}

		/**
		 * @brief executeTask
		 * @param taskFn
		 * @param finalizeFn
		 * @param groupId  an unique group id to be able to separate task finalization for different groups
		 *
		 * Can be safely called during a finalizeFn of another task
		 */
		template<typename TaskFnT, typename FinalizeFnT>
		void executeTask(TaskFnT&& taskFn, FinalizeFnT&& finalizeFn, size_t groupId = 0)
		{
			RACCOON_ECS_ASSERT(!mThreads.empty(), "No threads to execute the task");

			FinalizerGroup& group = getOrCreateFinalizerGroupAtomically(groupId);
			++group.tasksNotFinalizedCount;

			mTasksStack.enqueue_emplace(groupId, std::forward<TaskFnT>(taskFn), std::forward<FinalizeFnT>(finalizeFn));
		}

		template<typename TaskFnT, typename FinalizeFnT>
		void executeTasks(std::vector<std::pair<TaskFnT, FinalizeFnT>>&& tasks, size_t groupId = 0)
		{
			RACCOON_ECS_ASSERT(!mThreads.empty(), "No threads to execute the task");

			FinalizerGroup& group = getOrCreateFinalizerGroupAtomically(groupId);

			group.tasksNotFinalizedCount += tasks.size();

			for (auto& task : tasks)
			{
				mTasksStack.enqueue_emplace(groupId, std::move(task.first), std::move(task.second));
			}
		}

		/**
		 * @brief finalizeTasks
		 * @param groupId
		 */
		void finalizeTasks(size_t groupId = 0)
		{
			FinalizerGroup& finalizerGroup = getOrCreateFinalizerGroupAtomically(groupId);

			finalizeTaskForGroup(finalizerGroup);
		}

		void processAndFinalizeTasks(size_t groupId = 0)
		{
			std::vector<Finalizer> finalizersToExecute(24);
			Task currentTask;

			FinalizerGroup& finalizerGroup = getOrCreateFinalizerGroupAtomically(groupId);

			while(finalizerGroup.tasksNotFinalizedCount.load(std::memory_order::acquire) > 0)
			{
				if (size_t count = finalizerGroup.readyFinalizers.try_dequeue_bulk(finalizersToExecute.begin(), finalizersToExecute.size()); count > 0)
				{
					finalizerGroup.tasksNotFinalizedCount -= count;
					finalizeReadyTasks(finalizersToExecute, count);
				}
				else if (mTasksStack.try_dequeue(currentTask))
				{
					processAndFinalizeOneTask(groupId, currentTask);
				}
				else
				{
					std::this_thread::yield();
				}
			}
		}

		static size_t GetThisThreadId() { return ThisThreadId; }

		/**
		 * Safe to call if we do it after all spawnThreads calls
		 */
		size_t getThreadsCount() const { return mThreads.size(); }

	private:
		using TaskFn = std::function<std::any()>;
		using FinalizeFn = std::function<void(std::any&&)>;

		struct Finalizer
		{
			FinalizeFn fn;
			std::any result;
		};

		struct FinalizerGroup
		{
			std::atomic<int> tasksNotFinalizedCount = 0;
			moodycamel::ConcurrentQueue<Finalizer> readyFinalizers;
		};

		struct Task
		{
			Task() = default;

			template<typename TaskFnT, typename FinalizeFnT>
			Task(size_t groupId, TaskFnT&& taskFn, FinalizeFnT&& finalizeFn = nullptr)
				: groupId(groupId)
				, taskFn(std::move(taskFn))
				, finalizeFn(std::move(finalizeFn))
			{}

			size_t groupId;
			TaskFn taskFn;
			FinalizeFn finalizeFn;
		};

		void workerThreadFunction()
		{
			Task currentTask;

			while(!mReadyToShutdown.load(std::memory_order::acquire))
			{
				if (mTasksStack.try_dequeue(currentTask))
				{
					std::any result = currentTask.taskFn();

					taskPostProcess(currentTask, std::move(result));
				}
				else
				{
					std::this_thread::yield();
				}
			}

			if (mThreadPreShutdownTask)
			{
				mThreadPreShutdownTask();
			}
		}

		void taskPostProcess(Task& currentTask, std::any&& result)
		{
			if (currentTask.finalizeFn)
			{
				FinalizerGroup& finalizerGroup = getOrCreateFinalizerGroupAtomically(currentTask.groupId);
				finalizerGroup.readyFinalizers.enqueue_emplace(std::move(currentTask.finalizeFn), std::move(result));
			}
			else
			{
				size_t tasksLeftCount = 0;
				FinalizerGroup& finalizerGroup = getOrCreateFinalizerGroupAtomically(currentTask.groupId);
				tasksLeftCount = --finalizerGroup.tasksNotFinalizedCount;

				if (tasksLeftCount <= 0)
				{
					RACCOON_ECS_ASSERT(tasksLeftCount == 0, "finalizerGroup.tasksLeftCount should never be negative");
				}
			}
		}

		void finalizeTaskForGroup(FinalizerGroup& finalizerGroup)
		{
			std::vector<Finalizer> finalizersToExecute(24);

			while(finalizerGroup.tasksNotFinalizedCount.load(std::memory_order::acquire) > 0)
			{
				if (size_t count = finalizerGroup.readyFinalizers.try_dequeue_bulk(finalizersToExecute.begin(), finalizersToExecute.size()); count > 0)
				{
					finalizerGroup.tasksNotFinalizedCount -= count;
					finalizeReadyTasks(finalizersToExecute, count);
				}
			}
		}

		void finalizeReadyTasks(std::vector<Finalizer>& finalizersToExecute, size_t size)
		{
			if (!finalizersToExecute.empty())
			{
				for (size_t i = 0; i < size; ++i)
				{
					Finalizer& finalizer = finalizersToExecute[i];
					if (finalizer.fn)
					{
						finalizer.fn(std::move(finalizer.result));
					}
				}
			}
		}

		FinalizerGroup& getOrCreateFinalizerGroupAtomically(size_t groupId)
		{
			std::lock_guard<std::mutex> lock(mFinalizersMutex);
			std::unique_ptr<FinalizerGroup>& groupPtr = mFinalizers[groupId];

			if (!groupPtr)
			{
				groupPtr = std::make_unique<FinalizerGroup>();
			}
			return *groupPtr;
		}

		void processAndFinalizeOneTask(size_t groupId, Task& currentTask)
		{

			FinalizerGroup& finalizerGroup = getOrCreateFinalizerGroupAtomically(currentTask.groupId);

			std::any result = currentTask.taskFn();

			if (currentTask.groupId == groupId)
			{
				if (currentTask.finalizeFn)
				{
					currentTask.finalizeFn(std::move(result));
				}
				--finalizerGroup.tasksNotFinalizedCount;
			}
		}

	private:
		std::atomic_bool mReadyToShutdown = false;
		moodycamel::ConcurrentQueue<Task> mTasksStack;

		std::mutex mFinalizersMutex;
		std::unordered_map<size_t, std::unique_ptr<FinalizerGroup>> mFinalizers;

		std::vector<std::thread> mThreads;
		const std::function<void()> mThreadPreShutdownTask;

		static inline thread_local size_t ThisThreadId = 0;
	};
} // namespace RaccoonEcs
