#pragma once

#ifdef ECS_DEBUG_CHECKS_ENABLED

#include <functional>
#include <string>

namespace RaccoonEcs
{
	inline std::function<void(const std::string)> gErrorHandler = [](const std::string&){};
} // namespace RaccoonEcs

#endif // ECS_DEBUG_CHECKS_ENABLED
