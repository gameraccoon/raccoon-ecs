#pragma once

#if defined(_MSC_VER)
#define TEMPLATE_MSVC_FIX
#define TEMPLATE_MSVC_CLANG_FIX
#define TEMPLATE_MSVC_EMSCRIPTEN_FIX
#elif defined(__EMSCRIPTEN__)
#define TEMPLATE_MSVC_FIX template
#define TEMPLATE_MSVC_CLANG_FIX
#define TEMPLATE_MSVC_EMSCRIPTEN_FIX
#elif defined(__clang__)
#define TEMPLATE_MSVC_FIX template
#define TEMPLATE_MSVC_CLANG_FIX
#define TEMPLATE_MSVC_EMSCRIPTEN_FIX template
#else
#define TEMPLATE_MSVC_FIX template
#define TEMPLATE_MSVC_CLANG_FIX template
#define TEMPLATE_MSVC_EMSCRIPTEN_FIX template
#endif
