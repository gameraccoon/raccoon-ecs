#pragma once

// compile fix for MSVC that has different requirements
#ifdef _MSC_VER
#define TEMPLATE_MSVC_FIX
#else
#define TEMPLATE_MSVC_FIX template
#endif
