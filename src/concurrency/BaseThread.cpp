#include "Thread.h"
#include <assert.h>

namespace concurrency
{

void BaseThread::callRun(void *_this)
{
    ((BaseThread *)_this)->doRun();
}

} // namespace concurrency
