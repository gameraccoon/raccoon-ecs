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

		template<typename TaskFnT, typename FinalizeFnT>
		void submitTask(TaskFnT&& taskFn, FinalizeFnT&& finalizeFn = nullptr)
		{
			mPreparedTasks.emplace_back(std::forward<TaskFnT>(taskFn), std::forward<FinalizeFnT>(finalizeFn));
		}

		void executeAll()
		{
			int tasksLeftCount = mPreparedTasks.size();

			{
				std::unique_lock<std::mutex> l(mDataMutex);
				for (Task& preparedTask : mPreparedTasks)
				{
					mTasksQueue.push_back(std::move(preparedTask));
				}
			}
			mPreparedTasks.clear();

			mWakeUpWorkingThread.notify_all();

			while(tasksLeftCount > 0)
			{
				std::vector<FinalizeFn> finalizersToExecute;

				{
					std::unique_lock<std::mutex> l(mDataMutex);

					mFinalizatorAdded.wait(l, [this]{ return !mFinalizators.empty(); });

					while (!mFinalizators.empty())
					{
						finalizersToExecute.push_back(std::move(mFinalizators.front()));
						mFinalizators.pop_front();
					}
				}

				for (FinalizeFn& finalizer : finalizersToExecute)
				{
					if (finalizer)
					{
						finalizer();
					}
					--tasksLeftCount;
				}
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
					std::unique_lock<std::mutex> l(mDataMutex);

					// wait for a new task or for the shutdown
					mWakeUpWorkingThread.wait(l, [this]{ return !mTasksQueue.empty() || mReadyToShutdown; });

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
					mFinalizators.push_back(std::move(currentTask.finalizeFn));
				}

				mFinalizatorAdded.notify_all();
			}
		}

	private:
		std::condition_variable mWakeUpWorkingThread;
		std::condition_variable mFinalizatorAdded;
		std::mutex mDataMutex;
		bool mReadyToShutdown = false;
		std::vector<std::thread> mThreads;
		std::vector<Task> mPreparedTasks;
		std::list<Task> mTasksQueue;
		std::list<FinalizeFn> mFinalizators;
	};
} // namespace RaccoonEcs
