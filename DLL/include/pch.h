// pch.h: include files that are used frequently but rarely changed.
// This keeps IntelliSense and build times reasonable.

#ifndef PCH_H
#define PCH_H

// Disable ImGui runtime asserts to avoid breaking injection sessions.
#ifndef IM_ASSERT
#define IM_ASSERT(_EXPR) ((void)(_EXPR))
#endif

#include "framework.h"

#endif // PCH_H
