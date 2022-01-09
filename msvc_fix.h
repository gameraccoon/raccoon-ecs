#pragma once

// compile fix for MSVC that has different requirements
#ifdef COMPILER_MSVC
#define TEMPLATE_MSVC_FIX
#else
#define TEMPLATE_MSVC_FIX template
#endif
