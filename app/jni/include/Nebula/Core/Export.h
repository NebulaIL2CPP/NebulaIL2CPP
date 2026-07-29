#pragma once

#if defined(__GNUC__)
#define NEBULA_EXPORT __attribute__((visibility("default")))
#else
#define NEBULA_EXPORT
#endif
