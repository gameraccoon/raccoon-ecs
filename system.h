#pragma once

namespace RaccoonEcs
{
	/**
	 * @brief The base class for game Systems
	 *
	 * Abstract
	 *
	 * You don't have to use this class, you can implement your versions of System and SystemsManager
	 */
	class System
	{
	public:
		virtual ~System() = default;

		virtual void update() = 0;
		virtual void init() {}
		virtual void shutdown() {}
	};

} // namespace RaccoonEcs
