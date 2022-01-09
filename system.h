#pragma once

namespace RaccoonEcs
{
	/**
	 * The base class for game Systems
	 *
	 * Abstract
	 */
	class System
	{
	public:
		virtual ~System() = default;

		virtual void update() = 0;
		virtual void initResources() {}
		virtual void shutdown() {}
	};

} // namespace RaccoonEcs
