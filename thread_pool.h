#pragma once

#include <any>
#include <condition_variable>
#include <functional>
#include <list>
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

			{
				std::lock_guard<std::mutex> l(mDataMutex);

				FinalizerGroup& group = getOrCreateFinalizerGroup(groupId);
				++group.tasksNotStartedCount;
				++group.tasksNotFinalizedCount;

				mTasksStack.enqueue_emplace(groupId, std::forward<TaskFnT>(taskFn), std::forward<FinalizeFnT>(finalizeFn));
			}
		}

		template<typename TaskFnT, typename FinalizeFnT>
		void executeTasks(std::vector<std::pair<TaskFnT, FinalizeFnT>>&& tasks, size_t groupId = 0)
		{
			RACCOON_ECS_ASSERT(!mThreads.empty(), "No threads to execute the task");

			{
				std::lock_guard<std::mutex> l(mDataMutex);

				FinalizerGroup& group = getOrCreateFinalizerGroup(groupId);
				group.tasksNotStartedCount += tasks.size();
				group.tasksNotFinalizedCount += tasks.size();

				for (auto& task : tasks)
				{
					mTasksStack.enqueue_emplace(groupId, std::move(task.first), std::move(task.second));
				}
			}
		}

		/**
		 * @brief finalizeTasks
		 * @param groupId
		 */
		void finalizeTasks(size_t groupId = 0)
		{
			std::unique_lock<std::mutex> lock(mDataMutex);
			FinalizerGroup& finalizerGroup = getOrCreateFinalizerGroup(groupId);
			finalizeTaskForGroup(finalizerGroup, lock);
		}

		void processAndFinalizeTasks(size_t groupId = 0)
		{
			std::vector<Finalizer> finalizersToExecute;

			std::unique_lock<std::mutex> lock(mDataMutex);
			FinalizerGroup& finalizerGroup = getOrCreateFinalizerGroup(groupId);

			while(finalizerGroup.tasksNotFinalizedCount > 0)
			{
				while (finalizerGroup.readyFinalizers.empty() && finalizerGroup.tasksNotFinalizedCount > 0 && finalizerGroup.tasksNotStartedCount <= 0)
				{
					lock.unlock();
					std::this_thread::yield();
					lock.lock();
				}

				// limit finalizerGroup scope
				{
					if (finalizerGroup.tasksNotFinalizedCount <= 0)
					{
						RACCOON_ECS_ASSERT(finalizerGroup.tasksNotFinalizedCount == 0, "mTasksLeftCount should never be negative");
						break;
					}

					while (!finalizerGroup.readyFinalizers.empty())
					{
						finalizersToExecute.push_back(std::move(finalizerGroup.readyFinalizers.front()));
						finalizerGroup.readyFinalizers.pop_front();
						--finalizerGroup.tasksNotFinalizedCount;
					}

					// if we don't have any finalizers to process, process tasks
					if (finalizersToExecute.empty())
					{
						processAndFinalizeOneTask(groupId, lock);
					}
				}

				if (!finalizersToExecute.empty())
				{
					// finalizers can be executed without having the mutex locked
					lock.unlock();
					for (Finalizer& finalizer : finalizersToExecute)
					{
						if (finalizer.fn)
						{
							finalizer.fn(std::move(finalizer.result));
						}
					}
					finalizersToExecute.clear();
					// need to lock it here to protect tasksLeftCount in the loop check
					lock.lock();
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
			int tasksNotFinalizedCount = 0;
			int tasksNotStartedCount = 0;
			std::list<Finalizer> readyFinalizers;
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

			while(true)
			{
				const bool readyToShutdown = mReadyToShutdown.load(std::memory_order::acquire);

				if (readyToShutdown)
				{
					if (mThreadPreShutdownTask) {
						mThreadPreShutdownTask();
					}
					return;
				}

				const bool gotNewTask = mTasksStack.try_dequeue(currentTask);

				if (!gotNewTask)
				{
					std::this_thread::yield();
					continue;
				}

				{
					std::unique_lock<std::mutex> lock(mDataMutex);
					FinalizerGroup& finalizerGroup = getOrCreateFinalizerGroup(currentTask.groupId);
					--finalizerGroup.tasksNotStartedCount;
				}

				std::any result = currentTask.taskFn();

				taskPostProcess(currentTask, std::move(result));
			}
		}

		void taskPostProcess(Task& currentTask, std::any&& result)
		{
			if (currentTask.finalizeFn)
			{
				{
					std::unique_lock<std::mutex> lock(mDataMutex);
					FinalizerGroup& finalizerGroup = getOrCreateFinalizerGroup(currentTask.groupId);
					finalizerGroup.readyFinalizers.emplace_back(std::move(currentTask.finalizeFn), std::move(result));
				}
				mFinalizersChanged.notify_all();
			}
			else
			{
				size_t tasksLeftCount = 0;
				{
					std::unique_lock<std::mutex> lock(mDataMutex);
					FinalizerGroup& finalizerGroup = getOrCreateFinalizerGroup(currentTask.groupId);
					tasksLeftCount = --finalizerGroup.tasksNotFinalizedCount;
				}

				if (tasksLeftCount <= 0)
				{
					RACCOON_ECS_ASSERT(tasksLeftCount == 0, "finalizerGroup.tasksLeftCount should never be negative");
					mFinalizersChanged.notify_all();
				}
			}
		}

		void finalizeTaskForGroup(FinalizerGroup& finalizerGroup, std::unique_lock<std::mutex>& lock)
		{
			std::vector<Finalizer> finalizersToExecute;

			while(finalizerGroup.tasksNotFinalizedCount > 0)
			{
				mFinalizersChanged.wait(lock, [&finalizerGroup]{
					return !finalizerGroup.readyFinalizers.empty() || finalizerGroup.tasksNotFinalizedCount <= 0;
				});

				// limit finalizerGroup scope
				{
					if (finalizerGroup.tasksNotFinalizedCount <= 0)
					{
						RACCOON_ECS_ASSERT(finalizerGroup.tasksNotFinalizedCount == 0, "mTasksLeftCount should never be negative");
						break;
					}

					while (!finalizerGroup.readyFinalizers.empty())
					{
						finalizersToExecute.push_back(std::move(finalizerGroup.readyFinalizers.front()));
						finalizerGroup.readyFinalizers.pop_front();
						--finalizerGroup.tasksNotFinalizedCount;
					}
				}

				// finalizers can be executed without having the mutex locked
				lock.unlock();
				for (Finalizer& finalizer : finalizersToExecute)
				{
					if (finalizer.fn)
					{
						finalizer.fn(std::move(finalizer.result));
					}
				}
				finalizersToExecute.clear();
				// need to lock it here to protect tasksLeftCount in the loop check
				lock.lock();
			}
		}

		FinalizerGroup& getOrCreateFinalizerGroup(size_t groupId)
		{
			std::unique_ptr<FinalizerGroup>& groupPtr = mFinalizers[groupId];
			if (!groupPtr)
			{
				groupPtr = std::make_unique<FinalizerGroup>();
			}
			return *groupPtr;
		}

		void processAndFinalizeOneTask(size_t groupId, std::unique_lock<std::mutex>& lock)
		{
			Task currentTask;
			const bool gotNewTask = mTasksStack.try_dequeue(currentTask);

			if (gotNewTask)
			{
				FinalizerGroup& finalizerGroup = getOrCreateFinalizerGroup(currentTask.groupId);
				--finalizerGroup.tasksNotStartedCount;

				lock.unlock();
				std::any result = currentTask.taskFn();
				lock.lock();

				if (currentTask.groupId == groupId)
				{
					if (currentTask.finalizeFn)
					{
						currentTask.finalizeFn(std::move(result));
					}
					--finalizerGroup.tasksNotFinalizedCount;
				}
			}
		}

	private:
		std::condition_variable mFinalizersChanged;

		std::mutex mDataMutex;
		std::atomic_bool mReadyToShutdown = false;
		moodycamel::ConcurrentQueue<Task> mTasksStack;
		std::unordered_map<size_t, std::unique_ptr<FinalizerGroup>> mFinalizers;

		std::vector<std::thread> mThreads;
		std::function<void()> mThreadPreShutdownTask;

		static inline thread_local size_t ThisThreadId = 0;
	};
} // namespace RaccoonEcs
