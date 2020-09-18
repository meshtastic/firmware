#include "PosixNotifiedWorkerThread.h"

#ifdef __unix__

#include <Utility.h>

using namespace concurrency;

/**
 * Notify this thread so it can run
 */
void PosixNotifiedWorkerThread::notify(uint32_t v, eNotifyAction action) NOT_IMPLEMENTED("notify");

/**
 * A method that should block execution - either waiting ona queue/mutex or a "task notification"
 */
void PosixNotifiedWorkerThread::block() NOT_IMPLEMENTED("block");

#endif