#pragma once

#include <chrono>
#include <memory>
#include <vector>

#include "system.h"

namespace RaccoonEcs
{
	struct SystemsFrameTime
	{
		std::chrono::microseconds frameTime;
		std::vector<std::chrono::microseconds> systemsTime;
	};

	/**
	 * Manager for game systems
	 */
	class SystemsManager
	{
	public:
		template <typename T, typename... Args>
		void registerSystem(Args&&... args)
		{
			mSystems.emplace_back(new T(std::forward<Args>(args)...));
			mSystemIds.push_back(T::GetSystemId());
		}

		void update()
		{
#ifdef RACCOON_ECS_PROFILE_SYSTEMS
			mThisFrameTime.frameTime = std::chrono::microseconds::zero();
			mThisFrameTime.systemsTime.clear();
#endif // RACCOON_ECS_PROFILE_SYSTEMS

			for (std::unique_ptr<System>& system : mSystems)
			{
#ifdef RACCOON_ECS_PROFILE_SYSTEMS
				std::chrono::time_point<std::chrono::system_clock> start = std::chrono::system_clock::now();
#endif // RACCOON_ECS_PROFILE_SYSTEMS

				// real work is being done here
				system->update();

#ifdef RACCOON_ECS_PROFILE_SYSTEMS
				std::chrono::time_point<std::chrono::system_clock> end = std::chrono::system_clock::now();
				auto timeDiff = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
				mThisFrameTime.frameTime += timeDiff;
				mThisFrameTime.systemsTime.push_back(timeDiff);
#endif // RACCOON_ECS_PROFILE_SYSTEMS
			}

#ifdef RACCOON_ECS_PROFILE_SYSTEMS
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

#ifdef RACCOON_ECS_PROFILE_SYSTEMS
		SystemsFrameTime getPreviousFrameTimeData()
		{
			return mPreviousFrameTime;
		}
#endif // RACCOON_ECS_PROFILE_SYSTEMS

		const std::vector<std::string>& getSystemNames()
		{
			return mSystemIds;
		}

	private:
		std::vector<std::unique_ptr<System>> mSystems;
		std::vector<std::string> mSystemIds;

#ifdef RACCOON_ECS_PROFILE_SYSTEMS
		SystemsFrameTime mThisFrameTime;
		SystemsFrameTime mPreviousFrameTime;
#endif // RACCOON_ECS_PROFILE_SYSTEMS
	};

} // namespace RaccoonEcs
