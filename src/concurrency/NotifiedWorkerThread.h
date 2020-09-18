#pragma once

#include "FreeRtosNotifiedWorkerThread.h"
#include "PosixNotifiedWorkerThread.h"

namespace concurrency
{

#ifdef HAS_FREE_RTOS
typedef FreeRtosNotifiedWorkerThread NotifiedWorkerThread;
#endif

#ifdef __unix__
typedef PosixNotifiedWorkerThread NotifiedWorkerThread;
#endif

} // namespace concurrency
