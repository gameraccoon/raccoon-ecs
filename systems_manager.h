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
			for (std::unique_ptr<System>& system : mSystems)
			{
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

		const std::vector<std::string>& getSystemNames()
		{
			return mSystemIds;
		}

	private:
		std::vector<std::unique_ptr<System>> mSystems;
		std::vector<std::string> mSystemIds;
	};

} // namespace RaccoonEcs
