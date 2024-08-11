#pragma once

#ifdef RACCOON_ECS_DEBUG_CHECKS_ENABLED

#include <functional>
#include <string>

namespace RaccoonEcs
{
	inline std::function<void(const std::string)> gErrorHandler = [](const std::string&) {};

	template<typename T>
	std::string toString(const T& value)
	{
		using std::to_string;

		// to support printing your type create a function with the following signature:
		// std::string to_string(const YourType& value);
		// in the namespace of your type, or in the global namespace
		return to_string(value);
	}

	template<>
	inline std::string toString<>(const std::string& value)
	{
		return value;
	}

	template<>
	inline std::string toString<>(const char* const& value)
	{
		return value;
	}
} // namespace RaccoonEcs

#define RACCOON_ECS_ERROR(message) RaccoonEcs::gErrorHandler(message)

#define RACCOON_ECS_ASSERT(condition, message) \
	do \
	{ \
		if (static_cast<bool>(condition) == false) \
		{ \
			RACCOON_ECS_ERROR(message); \
		} \
	} while (0)

#else

#define RACCOON_ECS_ERROR(message) \
	do { \
	} while (0)
#define RACCOON_ECS_ASSERT(condition, message) \
	do { \
	} while (0)

#endif // RACCOON_ECS_DEBUG_CHECKS_ENABLED

#define RACCOON_ECS_COMPILE_ERROR(cond) typedef int assert##__LINE__[!!(condition) ? 1 : -1];
