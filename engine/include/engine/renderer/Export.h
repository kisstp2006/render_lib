#pragma once

#if defined(_WIN32)
#  if defined(ENGINE_RENDERER_SDK_BUILD)
#    define ENGINE_RENDERER_API __declspec(dllexport)
#  else
#    define ENGINE_RENDERER_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define ENGINE_RENDERER_API __attribute__((visibility("default")))
#else
#  define ENGINE_RENDERER_API
#endif
