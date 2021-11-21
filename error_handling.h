#pragma once

#ifdef RACCOON_ECS_DEBUG_CHECKS_ENABLED

#include <functional>
#include <string>

namespace RaccoonEcs
{
	inline std::function<void(const std::string)> gErrorHandler = [](const std::string&){};
} // namespace RaccoonEcs


#define RACCOON_ECS_ERROR(message) RaccoonEcs::gErrorHandler(message)

#define RACCOON_ECS_ASSERT(condition, message) \
do \
{ \
	if (static_cast<bool>(condition) == false) \
	{ \
		RACCOON_ECS_ERROR(message); \
	} \
} while(0)

#else

#define RACCOON_ECS_ERROR(message)
#define RACCOON_ECS_ASSERT(condition, message)

#endif // RACCOON_ECS_DEBUG_CHECKS_ENABLED

#define RACCOON_ECS_COMPILE_ERROR(cond) typedef int assert ## __LINE__ [!!(condition) ? 1 : -1];
