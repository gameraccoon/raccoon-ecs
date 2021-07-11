#pragma once

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

		void spawnThreads(size_t threadsCount)
		{
			for (size_t i = 0; i < threadsCount; ++i)
			{
				mThreads.emplace_back([this]{ workerThreadFunction(); });
			}
		}

		/**
		 * Can be called during a finalizeFn of another task
		 */
		template<typename TaskFnT, typename FinalizeFnT>
		void executeTask(TaskFnT&& taskFn, FinalizeFnT&& finalizeFn)
		{
			RACCOON_ECS_ASSERT(!mThreads.empty(), "No threads to execute the task");

			std::lock_guard<std::mutex> l(mDataMutex);
			mTasksQueue.emplace_back(std::forward<TaskFnT>(taskFn), std::forward<FinalizeFnT>(finalizeFn));
			++mTasksLeftCount;
			mWakeUpWorkingThread.notify_one();
		}

		/**
		 * This method should never be called from finalizers
		 */
		void finalizeTasks()
		{
			std::vector<FinalizeFn> finalizersToExecute;

			std::unique_lock<std::mutex> lock(mDataMutex);

			while(mTasksLeftCount > 0)
			{
				mWakeUpMainThread.wait(lock, [this]{ return !mFinalizators.empty() || mTasksLeftCount == 0; });

				if (mTasksLeftCount <= 0)
				{
					RACCOON_ECS_ASSERT(mTasksLeftCount == 0, "mTasksLeftCount should never be negative");
					break;
				}

				while (!mFinalizators.empty())
				{
					finalizersToExecute.push_back(std::move(mFinalizators.front()));
					mFinalizators.pop_front();
					--mTasksLeftCount;
				}

				// finalizers can be executed without having the mutex locked
				lock.unlock();
				for (FinalizeFn& finalizer : finalizersToExecute)
				{
					if (finalizer)
					{
						finalizer();
					}
				}
				finalizersToExecute.clear();
				// need to lock it here to protect mTasksLeftCount in the loop check
				lock.lock();
			}
		}

	private:
		using TaskFn = std::function<void()>;
		using FinalizeFn = std::function<void()>;

		struct Task
		{
			Task() = default;

			template<typename TaskFnT, typename FinalizeFnT>
			Task(TaskFnT&& taskFn, FinalizeFnT&& finalizeFn = nullptr)
				: taskFn(std::move(taskFn))
				, finalizeFn(std::move(finalizeFn))
			{}

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
					mWakeUpWorkingThread.wait(lock, [this]{ return !mTasksQueue.empty() || mReadyToShutdown; });

					if (mReadyToShutdown)
					{
						return;
					}

					currentTask = std::move(mTasksQueue.front());
					mTasksQueue.pop_front();
				}

				currentTask.taskFn();

				{
					std::lock_guard<std::mutex> l(mDataMutex);
					if (currentTask.finalizeFn)
					{
						mFinalizators.push_back(std::move(currentTask.finalizeFn));
						mWakeUpMainThread.notify_all();
					}
					else
					{
						--mTasksLeftCount;
						if (mTasksLeftCount <= 0)
						{
							RACCOON_ECS_ASSERT(mTasksLeftCount == 0, "mTasksLeftCount should never be negative");
							mWakeUpMainThread.notify_all();
						}
					}
				}
			}
		}

	private:
		std::condition_variable mWakeUpWorkingThread;
		std::condition_variable mWakeUpMainThread;
		std::mutex mDataMutex;
		bool mReadyToShutdown = false;
		std::vector<std::thread> mThreads;
		int mTasksLeftCount = 0;
		std::list<Task> mTasksQueue;
		std::list<FinalizeFn> mFinalizators;
	};
} // namespace RaccoonEcs
