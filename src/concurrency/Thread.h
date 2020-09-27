#pragma once

#include "FreeRtosThread.h"
#include "PosixThread.h"

namespace concurrency
{

#ifdef HAS_FREE_RTOS
typedef FreeRtosThread Thread;
#endif

#ifdef __unix__
typedef PosixThread Thread;
#endif

} // namespace concurrency
