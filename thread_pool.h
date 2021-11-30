#pragma once

#include <any>
#include <condition_variable>
#include <functional>
#include <list>
#include <thread>
#include <vector>

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
			{
				std::lock_guard<std::mutex> l(mDataMutex);
				mReadyToShutdown = true;
			}

			mTasksOrFinalizersChanged.notify_all();
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

				mTasksQueue.emplace_back(groupId, std::forward<TaskFnT>(taskFn), std::forward<FinalizeFnT>(finalizeFn));
			}
			mTasksOrFinalizersChanged.notify_one();
		}

		template<typename TaskFnT, typename FinalizeFnT>
		void executeTasks(std::vector<std::pair<TaskFnT, FinalizeFnT>>&& tasks, size_t groupId = 0)
		{
			RACCOON_ECS_ASSERT(!mThreads.empty(), "No threads to execute the task");

			const size_t tasksCount = tasks.size();

			{
				std::lock_guard<std::mutex> l(mDataMutex);

				FinalizerGroup& group = getOrCreateFinalizerGroup(groupId);
				group.tasksNotStartedCount += static_cast<int>(tasks.size());
				group.tasksNotFinalizedCount += static_cast<int>(tasks.size());

				for (auto& task : tasks)
				{
					mTasksQueue.emplace_back(groupId, std::move(task.first), std::move(task.second));
				}
			}

			if (tasksCount >= mThreads.size())
			{
				mTasksOrFinalizersChanged.notify_all();
			}
			else
			{
				for (size_t i = 0; i < tasksCount; ++i)
				{
					mTasksOrFinalizersChanged.notify_one();
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
				mTasksOrFinalizersChanged.wait(lock, [&finalizerGroup]{
					return !finalizerGroup.readyFinalizers.empty() || finalizerGroup.tasksNotFinalizedCount <= 0 || finalizerGroup.tasksNotStartedCount > 0;
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

					// if we don't have any finalizers to process, process tasks
					if (finalizersToExecute.empty() && !mTasksQueue.empty())
					{
						processAndFinalizeOneTask(finalizerGroup, groupId, lock);
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

			size_t groupId = 0;
			TaskFn taskFn = nullptr;
			FinalizeFn finalizeFn = nullptr;
		};

		void workerThreadFunction()
		{
			Task currentTask;

			while(true)
			{
				{
					std::unique_lock<std::mutex> lock(mDataMutex);

					// wait for a new task or for the shutdown
					mTasksOrFinalizersChanged.wait(lock,
						[this]{
							return !mTasksQueue.empty() || mReadyToShutdown;
						}
					);

					if (mReadyToShutdown)
					{
						if (mThreadPreShutdownTask) {
							mThreadPreShutdownTask();
						}
						return;
					}

					currentTask = std::move(mTasksQueue.front());
					mTasksQueue.pop_front();

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
				mTasksOrFinalizersChanged.notify_all();
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
					mTasksOrFinalizersChanged.notify_all();
				}
			}
		}

		void finalizeTaskForGroup(FinalizerGroup& finalizerGroup, std::unique_lock<std::mutex>& lock)
		{
			std::vector<Finalizer> finalizersToExecute;

			while(finalizerGroup.tasksNotFinalizedCount > 0)
			{
				mTasksOrFinalizersChanged.wait(lock, [&finalizerGroup]{
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

		void processAndFinalizeOneTask(FinalizerGroup& finalizerGroup, size_t groupId, std::unique_lock<std::mutex>& lock)
		{
			for (auto it = mTasksQueue.begin(); it != mTasksQueue.end(); ++it)
			{
				if (it->groupId == groupId)
				{
					Task currentTask = std::move(*it);
					mTasksQueue.erase(it);

					--finalizerGroup.tasksNotStartedCount;

					lock.unlock();
					std::any result = currentTask.taskFn();
					lock.lock();

					if (currentTask.finalizeFn)
					{
						currentTask.finalizeFn(std::move(result));
					}
					--finalizerGroup.tasksNotFinalizedCount;
					break;
				}
			}
		}

	private:
		std::condition_variable mTasksOrFinalizersChanged;

		std::mutex mDataMutex;
		bool mReadyToShutdown = false;
		std::list<Task> mTasksQueue;
		std::unordered_map<size_t, std::unique_ptr<FinalizerGroup>> mFinalizers;

		std::vector<std::thread> mThreads;
		std::function<void()> mThreadPreShutdownTask;

		static inline thread_local size_t ThisThreadId = 0;
	};
} // namespace RaccoonEcs
