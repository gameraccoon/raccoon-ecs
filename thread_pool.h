#pragma once

#include <any>
#include <atomic>
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
		ThreadPool() = default;

		ThreadPool(size_t threadsCount)
		{
			spawnThreads(threadsCount);
		}

		~ThreadPool()
		{
			{
				std::lock_guard<std::mutex> l(mDataMutex);
				mReadyToShutdown = true;
			}

			mWakeUpWorkingThread.notify_all();
			for (auto& thread : mThreads)
			{
				thread.join();
			}
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
				{
					std::lock_guard<std::mutex> l(group.dataMutex);
					++group.tasksLeftCount;
				}

				mTasksQueue.emplace_back(groupId, std::forward<TaskFnT>(taskFn), std::forward<FinalizeFnT>(finalizeFn));
			}
			mWakeUpWorkingThread.notify_one();
		}

		template<typename TaskFnT, typename FinalizeFnT>
		void executeTasks(std::vector<std::pair<TaskFnT, FinalizeFnT>>&& tasks, size_t groupId = 0)
		{
			RACCOON_ECS_ASSERT(!mThreads.empty(), "No threads to execute the task");

			{
				std::lock_guard<std::mutex> l(mDataMutex);

				FinalizerGroup& group = getOrCreateFinalizerGroup(groupId);
				{
					std::lock_guard<std::mutex> l(group.dataMutex);
					group.tasksLeftCount += tasks.size();
				}

				for (auto& task : tasks)
				{
					mTasksQueue.emplace_back(groupId, std::move(task.first), std::move(task.second));
				}
			}
			mWakeUpWorkingThread.notify_one();
		}

		/**
		 * @brief finalizeTasks
		 * @param groupId
		 */
		void finalizeTasks(size_t groupId = 0)
		{

			mDataMutex.lock();
			FinalizerGroup& finalizerGroup = getOrCreateFinalizerGroup(groupId);
			mDataMutex.unlock();

			finalizeTaskForGroup(finalizerGroup);
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
			std::mutex dataMutex;
			int tasksLeftCount = 0;
			std::list<Finalizer> readyFinalizers;
			std::condition_variable wakeUpFinalizerThread;
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
					mWakeUpWorkingThread.wait(lock,
						[this]{
							return !mTasksQueue.empty() || mReadyToShutdown;
						});

					if (mReadyToShutdown)
					{
						return;
					}

					currentTask = std::move(mTasksQueue.front());
					mTasksQueue.pop_front();
				}

				std::any result = currentTask.taskFn();

				{
					mDataMutex.lock();
					FinalizerGroup& finalizerGroup = getOrCreateFinalizerGroup(currentTask.groupId);
					mDataMutex.unlock();

					if (currentTask.finalizeFn)
					{
						{
							std::unique_lock<std::mutex> lock(finalizerGroup.dataMutex);
							finalizerGroup.readyFinalizers.emplace_back(std::move(currentTask.finalizeFn), std::move(result));
						}
						finalizerGroup.wakeUpFinalizerThread.notify_all();
					}
					else
					{
						{
							size_t tasksLeftCount = 0;
							{
								std::unique_lock<std::mutex> lock(finalizerGroup.dataMutex);
								tasksLeftCount = --finalizerGroup.tasksLeftCount;
							}

							if (tasksLeftCount <= 0)
							{
								RACCOON_ECS_ASSERT(tasksLeftCount == 0, "finalizerGroup.tasksLeftCount should never be negative");
								finalizerGroup.wakeUpFinalizerThread.notify_all();
							}
						}
					}
				}
			}
		}

		static void finalizeTaskForGroup(FinalizerGroup& finalizerGroup)
		{

			std::vector<Finalizer> finalizersToExecute;
			std::unique_lock<std::mutex> lock(finalizerGroup.dataMutex);

			while(finalizerGroup.tasksLeftCount > 0)
			{
				finalizerGroup.wakeUpFinalizerThread.wait(lock, [&finalizerGroup]{
					return !finalizerGroup.readyFinalizers.empty() || finalizerGroup.tasksLeftCount == 0;
				});

				// limit finalizerGroup scope
				{
					if (finalizerGroup.tasksLeftCount <= 0)
					{
						RACCOON_ECS_ASSERT(finalizerGroup.tasksLeftCount == 0, "mTasksLeftCount should never be negative");
						break;
					}

					while (!finalizerGroup.readyFinalizers.empty())
					{
						finalizersToExecute.push_back(std::move(finalizerGroup.readyFinalizers.front()));
						finalizerGroup.readyFinalizers.pop_front();
						--finalizerGroup.tasksLeftCount;
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

	private:
		std::condition_variable mWakeUpWorkingThread;

		std::mutex mDataMutex;
		bool mReadyToShutdown = false;
		std::list<Task> mTasksQueue;
		std::unordered_map<size_t, std::unique_ptr<FinalizerGroup>> mFinalizers;

		std::vector<std::thread> mThreads;

		static inline thread_local size_t ThisThreadId = 0;
	};
} // namespace RaccoonEcs
