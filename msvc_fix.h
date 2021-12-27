#pragma once

#ifdef COMPILER_MSVC
#define TEMPLATE_MSVC_FIX
#else
#define TEMPLATE_MSVC_FIX template
#endif
