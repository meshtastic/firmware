#include "SimpleAllocator.h"
#include "assert.h"
#include "configuration.h"

SimpleAllocator::SimpleAllocator()
{
    reset();
}

void *SimpleAllocator::alloc(size_t size)
{
    assert(nextFree + size <= sizeof(bytes));
    void *res = &bytes[nextFree];
    nextFree += size;
    LOG_DEBUG("Total simple allocs %u\n", nextFree);

    return res;
}

void SimpleAllocator::reset()
{
    nextFree = 0;
}

void *operator new(size_t size, SimpleAllocator &p)
{
    return p.alloc(size);
}

#if 0 
// This was a dumb idea, turn off for now

SimpleAllocator *activeAllocator;

AllocatorScope::AllocatorScope(SimpleAllocator &a)
{
    assert(!activeAllocator);
    activeAllocator = &a;
}

AllocatorScope::~AllocatorScope()
{
    assert(activeAllocator);
    activeAllocator = NULL;
}

/// Global new/delete, uses a simple allocator if it is in scope

void *operator new(size_t sz) throw(std::bad_alloc)
{
    void *mem = activeAllocator ? activeAllocator->alloc(sz) : malloc(sz);
    if (mem)
        return mem;
    else
        throw std::bad_alloc();
}

void operator delete(void *ptr) throw()
{
    if (activeAllocator)
        LOG_WARN("Leaking an active allocator object\n"); // We don't properly handle this yet
    else
        free(ptr);
}

#endif