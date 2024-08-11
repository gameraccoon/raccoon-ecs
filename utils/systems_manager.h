#pragma once

#include <memory>
#include <vector>

#include "system.h"

namespace RaccoonEcs
{
	/**
	 * @brief Simple manager for game systems
	 *
	 * You don't have to use this class (e.g. if you want to run some systems in parallel)
	 */
	class SystemsManager
	{
	public:
		template<typename T, typename... Args>
		void registerSystem(Args&&... args)
		{
			mSystems.emplace_back(new T(std::forward<Args>(args)...));
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
				system->init();
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

	private:
		std::vector<std::unique_ptr<System>> mSystems;
	};

} // namespace RaccoonEcs
