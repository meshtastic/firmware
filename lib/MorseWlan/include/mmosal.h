/*
 * Copyright 2021-2023 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @defgroup MMOSAL Morse Micro Operating System Abstraction Layer (mmosal) API
 *
 * This API provides a layer of abstraction from the underlying operation system. Functionality
 * is provided for typical RTOS features.
 *
 * @{
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mmport.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup MMOSAL_MAIN Main loop
 *
 * Provides the "main" function that runs the operating system scheduler.
 *
 * @{
 */

/** Application initialization callback (see @ref mmosal_main for details). */
typedef void (*mmosal_app_init_cb_t)(void);

/**
 * OS main function.
 *
 * This should be invoked after early initialization. If further initialization is required after
 * the scheduler is started an optional @p app_init_cb argument can be provided and the given
 * function with be executed in a thread that will subsequently be destroyed if it returns.
 *
 * @param app_init_cb   Application initialization callback that will be invoked after the
 *                      scheduler has been started. This optional (may be NULL). If specified,
 *                      the function will run in its own thread and may or may not return.
 *
 * @returns an error code if something goes wrong, otherwise does not return.
 */
int mmosal_main(mmosal_app_init_cb_t app_init_cb);

/**
 * @}
 */

/*
 * ---------------------------------------------------------------------------------------------
 */

/**
 * @defgroup MMOSAL_MEMMGMT Memory management
 *
 * Provides memory management functionality (allocation and free).
 *
 * @{
 */

/**
 * Allocate memory of the given size and return a pointer to it (malloc).
 *
 * No guarantees made about initialization.
 *
 * @warning This function should not be used directly. Instead use @ref mmosal_malloc().
 *
 * @param size  The amount of memory to allocate (in bytes).
 *
 * @returns pointer to the allocated memory on success, or NULL on failure.
 */
void *mmosal_malloc_(size_t size);

/**
 * Allocate memory of the given size and return a pointer to it (malloc) -- debug version.
 *
 * No guarantees made about initialization.
 *
 * @warning This function should not be used directly. Instead use @ref mmosal_malloc().
 *
 * @param size  The amount of memory to allocate (in bytes).
 * @param name  Name of the function that allocated the memory.
 * @param line_number   The line number at which the allocation happened.
 *
 * @returns pointer to the allocated memory on success, or NULL on failure.
 */
void *mmosal_malloc_dbg(size_t size, const char *name, unsigned line_number);

/**
 * Free the given memory allocation.
 *
 * @param p     The memory to free.
 */
void mmosal_free(void *p);

/**
 * Equivalent of standard library realloc().
 *
 * @param ptr   Previously allocated memory block to resize.
 * @param size  The new size.
 *
 * @returns pointer to the resized block of memory, or @c NULL.
 */
void *mmosal_realloc(void *ptr, size_t size);

/**
 * Equivalent of standard library calloc().
 *
 * @param nitems   Number of blocks to allocate
 * @param size     Size of each block.
 *
 * @returns pointer to the allocated memory on success, or NULL on failure.
 */
void *mmosal_calloc(size_t nitems, size_t size);

#ifndef MMOSAL_TRACK_ALLOCATIONS
/**
 * Allocate memory of the given size and return a pointer to it (malloc).
 *
 * No guarantees made about initialization.
 *
 * @param size  The amount of memory to allocate (in bytes).
 *
 * @returns pointer to the allocated memory on success, or NULL on failure.
 */
#define mmosal_malloc(size) mmosal_malloc_(size)
#else
/* Note: we use function name because it should be shorter than the full filename path. */
/**
 * Allocate memory of the given size and return a pointer to it (malloc).
 *
 * No guarantees made about initialization.
 *
 * @param size  The amount of memory to allocate (in bytes).
 *
 * @returns pointer to the allocated memory on success, or NULL on failure.
 */
#define mmosal_malloc(size) mmosal_malloc_dbg(size, __FUNCTION__, __LINE__)
#endif

/**
 * @}
 */

/*
 * ---------------------------------------------------------------------------------------------
 */

/**
 * @defgroup MMOSAL_TASKS Task (thread) management
 *
 * Provides memory management functionality managing tasks (create, destroy, yield, etc.).
 *
 * @{
 */

/**
 * Type definition for task main functions.
 *
 * @param arg   Opaque argument passed in at task creation.
 */
typedef void (*mmosal_task_fn_t)(void *arg);

/** Enumeration of task priorities (ordered lowest to highest). */
enum mmosal_task_priority {
    /** Idle task priority. */
    MMOSAL_TASK_PRI_IDLE,
    /** Minimum priority. */
    MMOSAL_TASK_PRI_MIN,
    /** Low priority. */
    MMOSAL_TASK_PRI_LOW,
    /** Normal priority. */
    MMOSAL_TASK_PRI_NORM,
    /** High priority. */
    MMOSAL_TASK_PRI_HIGH,
};

/**
 * Create a new task.
 *
 * @param task_fn           Task main function.
 * @param argument          Argument to pass to main function.
 * @param priority          Task priority.
 * @param stack_size_u32    Size of stack to allocate for task (in units of 32 bit words).
 * @param name              Name to give the task.
 *
 * @returns an opaque task handle, or @c NULL on failure.
 */
struct mmosal_task *mmosal_task_create(mmosal_task_fn_t task_fn, void *argument, enum mmosal_task_priority priority,
                                       unsigned stack_size_u32, const char *name);

/**
 * Delete the given task.
 *
 * @param task  Handle of the task to delete (or NULL to delete the current task).
 *
 * @note With FreeRTOS deleted tasks will not be cleaned up until the idle task runs.
 */
void mmosal_task_delete(struct mmosal_task *task);

/**
 * Block until the given task has terminated.
 *
 * @param task  Handle of the task to wait for.
 *
 * @note With FreeRTOS deleted tasks will not be cleaned up until the idle task runs.
 *
 * @deprecated Do not invoke this function because it is deprecated and will be removed from
 *             the mmosal API in a future release.
 */
void mmosal_task_join(struct mmosal_task *task);

/**
 * Get the handle of the active task.
 *
 * @returns the handle of the active task.
 */
struct mmosal_task *mmosal_task_get_active(void);

/**
 * Yield the active task.
 */
void mmosal_task_yield(void);

/**
 * Sleep for a period of time, yielding during that time.
 *
 * @param duration_ms   Sleep duration, in milliseconds.
 */
void mmosal_task_sleep(uint32_t duration_ms);

/**
 * Enter critical section.
 *
 * This may result in interrupts being disabled so critical sections must be kept short.
 * Each entry to a critical section must be matched to a corresponding exit
 * (@ref MMOSAL_TASK_EXIT_CRITICAL()).
 */
#define MMOSAL_TASK_ENTER_CRITICAL()                                                                                             \
    do {                                                                                                                         \
        MMPORT_MEM_SYNC();                                                                                                       \
        mmosal_task_enter_critical();                                                                                            \
    } while (0)

/**
 * Exit critical section.
 *
 * Used to exit a critical section previously entered with @ref MMOSAL_TASK_ENTER_CRITICAL()).
 */
#define MMOSAL_TASK_EXIT_CRITICAL()                                                                                              \
    do {                                                                                                                         \
        MMPORT_MEM_SYNC();                                                                                                       \
        mmosal_task_exit_critical();                                                                                             \
    } while (0)

/**
 * Enter critical section.
 *
 * This may result in interrupts being disabled so critical sections must be kept short.
 * Each entry to a critical section must be matched to a corresponding exit
 * (@ref mmosal_task_exit_critical()).
 *
 * @note This function should not be invoked directly. Use @ref MMOSAL_TASK_ENTER_CRITICAL().
 */
void mmosal_task_enter_critical(void);

/**
 * Exit critical section.
 *
 * Used to exit a critical section previously entered with @ref mmosal_task_enter_critical()).
 *
 * @note This function should not be invoked directly. Use @ref MMOSAL_TASK_EXIT_CRITICAL().
 */
void mmosal_task_exit_critical(void);

/**
 * Disable interrupts.
 *
 * @warning This function should generally not be used. Use @ref mmosal_task_enter_critical()
 * instead.
 */
void mmosal_disable_interrupts(void);

/**
 * Enable interrupts.
 *
 * @warning This function should generally not be used. Use @ref mmosal_task_exit_critical()
 * instead.
 */
void mmosal_enable_interrupts(void);

/**
 * Get the name of the running task.
 *
 * @returns the name of the running task.
 */
const char *mmosal_task_name(void);

/**
 * Blocks the current task until a notification is received.
 *
 * Notifications can be sent using @ref mmosal_task_notify() or @ref mmosal_task_notify_from_isr().
 *
 * @param timeout_ms    The time to wait before giving up (or @c UINT32_MAX to wait forever).
 *
 * @returns @c true of a notification was received or @c false if it timed out.
 *
 * @see mmosal_task_notify()
 * @see mmosal_task_notify_from_isr()
 */
bool mmosal_task_wait_for_notification(uint32_t timeout_ms);

/**
 * Notifies a waiting task (@ref mmosal_task_wait_for_notification()) that it can continue.
 *
 * @warning Must not be called from ISR context.
 *
 * @param task  Handle of the task to notify.
 *
 * @see mmosal_task_wait_for_notification()
 */
void mmosal_task_notify(struct mmosal_task *task);

/**
 * Notifies a waiting task (@ref mmosal_task_wait_for_notification()) that it can continue.
 *
 * @warning Must only be called from ISR context.
 *
 * @param task  Handle of the task to notify.
 *
 * @see mmosal_task_wait_for_notification()
 */
void mmosal_task_notify_from_isr(struct mmosal_task *task);

/**
 * @}
 */

/*
 * ---------------------------------------------------------------------------------------------
 */

/**
 * @defgroup MMOSAL_MUTEX Mutex
 *
 * Provides mutex support for mutual exclusion.
 *
 * @{
 */

/**
 * Create a new mutex.
 *
 * @param name  The name for the mutex.
 *
 * @returns an opaque handle to the mutex, or @c NULL on failure.
 */
struct mmosal_mutex *mmosal_mutex_create(const char *name);

/**
 * Delete a mutex.
 *
 * @param mutex Handle of mutex to delete
 */
void mmosal_mutex_delete(struct mmosal_mutex *mutex);

/**
 * Acquire a mutex.
 *
 * @param mutex         Handle of mutex to acquire.
 * @param timeout_ms    Timeout after which to give up. To wait infinitely use @c UINT32_MAX.
 *
 * @returns @c true if the mutex was acquired successfully otherwise @c false.
 */
bool mmosal_mutex_get(struct mmosal_mutex *mutex, uint32_t timeout_ms);

/**
 * Release a mutex.
 *
 * @param mutex         Handle of mutex to release.
 *
 * @returns @c true if the mutex was released successfully otherwise @c false.
 */
bool mmosal_mutex_release(struct mmosal_mutex *mutex);

/**
 * Attempt to get a mutex, waiting infinitely long for it. Panics on failure.
 */
#define MMOSAL_MUTEX_GET_INF(_mutex)                                                                                             \
    do {                                                                                                                         \
        bool ok__ = mmosal_mutex_get((_mutex), UINT32_MAX);                                                                      \
        MMOSAL_ASSERT(ok__);                                                                                                     \
    } while (0)

/**
 * Same as @ref mmosal_mutex_release() except it panics on failure.
 */
#define MMOSAL_MUTEX_RELEASE(_mutex)                                                                                             \
    do {                                                                                                                         \
        bool ok__ = mmosal_mutex_release(_mutex);                                                                                \
        MMOSAL_ASSERT(ok__);                                                                                                     \
    } while (0)

/**
 * Check whether the given mutex is held by the active thread.
 *
 * @param mutex         Handle of the mutex to check.
 *
 * @returns @c true if the mutex is held by the active thread, else @c false.
 */
bool mmosal_mutex_is_held_by_active_task(struct mmosal_mutex *mutex);

/**
 * @}
 */

/*
 * ---------------------------------------------------------------------------------------------
 */

/**
 * @defgroup MMOSAL_SEM Counting semaphores
 *
 * Provides counting semaphore support for task synchronization.
 *
 * @{
 */

/**
 * Create a new counting semaphore.
 *
 * @param max_count     The maximum count (i.e., number of times the semaphore can be given).
 * @param initial_count The initial count (i.e., number of times the semaphore is implicitly
 *                      given when it is created).
 * @param name          The name of the semaphore.
 *
 * @returns an opaque handle to the semaphore, or @c NULL on failure.
 */
struct mmosal_sem *mmosal_sem_create(unsigned max_count, unsigned initial_count, const char *name);

/**
 * Delete the given counting semaphore.
 *
 * @param sem   Semaphore to delete.
 */
void mmosal_sem_delete(struct mmosal_sem *sem);

/**
 * Give a counting semaphore.
 *
 * @warning May not be invoked from an ISR.
 *
 * @param sem   The semaphore to give.
 *
 * @returns @c true on success, else @c false.
 */
bool mmosal_sem_give(struct mmosal_sem *sem);

/**
 * Give a counting semaphore (from ISR context).
 *
 * @warning May only be invoked from an ISR.
 *
 * @param sem   The semaphore to give.
 *
 * @returns @c true on success, else @c false.
 */
bool mmosal_sem_give_from_isr(struct mmosal_sem *sem);

/**
 * Wait for a counting semaphore.
 *
 * @param sem           The semaphore to wait for.
 * @param timeout_ms    Timeout after which to give up waiting (in milliseconds).
 *
 * @returns @c true if the the semaphore was taken successfully, else @c false.
 */
bool mmosal_sem_wait(struct mmosal_sem *sem, uint32_t timeout_ms);

/**
 * Returns the current count of the semaphore.
 *
 * @param sem   The semaphore being queried.
 *
 * @returns The count of the semaphore being queried.
 */
uint32_t mmosal_sem_get_count(struct mmosal_sem *sem);

/**
 * @}
 */

/*
 * ---------------------------------------------------------------------------------------------
 */

/**
 * @defgroup MMOSAL_SEMB Binary semaphores
 *
 * Provides binary semaphore support for task synchronization.
 *
 * @{
 */

/**
 * Create a new binary semaphore.
 *
 * @param name  The name of the semaphore.
 *
 * @returns an opaque handle to the semaphore, or @c NULL on failure.
 */
struct mmosal_semb *mmosal_semb_create(const char *name);

/**
 * Delete the given binary semaphore.
 *
 * @param semb  Semaphore to delete.
 */
void mmosal_semb_delete(struct mmosal_semb *semb);

/**
 * Give a binary semaphore.
 *
 * @warning May not be invoked from an ISR.
 *
 * @param semb  The semaphore to give.
 *
 * @returns @c true on success, else @c false.
 */
bool mmosal_semb_give(struct mmosal_semb *semb);

/**
 * Give a binary semaphore (from ISR context).
 *
 * @warning May only be invoked from an ISR.
 *
 * @param semb  The semaphore to give.
 *
 * @returns @c true on success, else @c false.
 */
bool mmosal_semb_give_from_isr(struct mmosal_semb *semb);

/**
 * Wait for a counting semaphore.
 *
 * @param semb          The semaphore to wait for.
 * @param timeout_ms    Timeout after which to give up waiting (in milliseconds).
 *
 * @returns @c true if the the semaphore was taken successfully, else @c false.
 */
bool mmosal_semb_wait(struct mmosal_semb *semb, uint32_t timeout_ms);

/**
 * @}
 */

/*
 * ---------------------------------------------------------------------------------------------
 */

/**
 * @defgroup MMOSAL_QUEUE Queues (aka pipes)
 *
 * Provides queue support for inter-task communication.
 *
 * @{
 */

/**
 * Create a new queue.
 *
 * @param num_items The maximum number of items that may be in the queue at a time.
 * @param item_size The size of each item in the queue.
 * @param name      The name of the queue.
 *
 * @returns an opaque handle to the queue, or @c NULL on failure.
 */
struct mmosal_queue *mmosal_queue_create(size_t num_items, size_t item_size, const char *name);

/**
 * Delete a queue.
 *
 * @param queue handle of the queue to delete.
 */
void mmosal_queue_delete(struct mmosal_queue *queue);

/**
 * Pop an item from the queue.
 *
 * @warning May not be invoked from an ISR.
 *
 * @param queue         The queue to pop from.
 * @param item          Pointer to memory to receive the popped item. The item will be copied into
 *                      this memory. The memory size must match the @c item_size given at creation.
 * @param timeout_ms    Timeout after which to give up waiting for an item if the queue is empty
 *                      (in milliseconds).
 *
 * @returns @c true if an item was successfully popped, else @c false.
 */
bool mmosal_queue_pop(struct mmosal_queue *queue, void *item, uint32_t timeout_ms);

/**
 * Push an item into the queue.
 *
 * @warning May not be invoked from an ISR.
 *
 * @param queue         The queue to push to.
 * @param item          Pointer to the item to push. This item will be copied into the queue.
 *                      The size of the item must match the @c item_size given at creation.
 * @param timeout_ms    Timeout after which to give up waiting for space if the queue is full
 *                      (in milliseconds).
 *
 * @returns @c true if an item was successfully pushed, else @c false.
 */
bool mmosal_queue_push(struct mmosal_queue *queue, const void *item, uint32_t timeout_ms);

/**
 * Pop an item from the queue (from ISR context).
 *
 * @warning May only be invoked from an ISR.
 *
 * @param queue         The queue to pop from.
 * @param item          Pointer to memory to receive the popped item. The item will be copied into
 *                      this memory. The memory size must match the @c item_size given at creation.
 *
 * @returns @c true if an item was successfully popped, else @c false.
 */
bool mmosal_queue_pop_from_isr(struct mmosal_queue *queue, void *item);

/**
 * Push an item into the queue (from ISR context).
 *
 * @warning May only be invoked from an ISR.
 *
 * @param queue         The queue to push to.
 * @param item          Pointer to the item to push. This item will be copied into the queue.
 *                      The size of the item must match the @c item_size given at creation.
 *
 * @returns @c true if an item was successfully pushed, else @c false.
 */
bool mmosal_queue_push_from_isr(struct mmosal_queue *queue, const void *item);

/**
 * @}
 */

/*
 * ---------------------------------------------------------------------------------------------
 */

/**
 * @defgroup MMOSAL_TIME Time
 *
 * Provides support for reading the system time.
 *
 * @{
 */

/**
 * Get the system time in milliseconds.
 *
 * @returns the system time in milliseconds.
 */
uint32_t mmosal_get_time_ms(void);

/**
 * Get the system time in ticks.
 *
 * @returns the system time in ticks.
 */
uint32_t mmosal_get_time_ticks(void);

/**
 * Get the number of ticks in a second.
 *
 * @returns the number of ticks in a second.
 */
uint32_t mmosal_ticks_per_second(void);

/**
 * Check if time a is less than time b, taking into account wrapping.
 *
 * |      a        |       b       | return |
 * |---------------|---------------|--------|
 * |             0 |             0 | false  |
 * |             0 |             1 | true   |
 * |             1 |             0 | false  |
 * | @c 0xffffffff |             0 | true   |
 * |             0 | @c 0xffffffff | false  |
 *
 *
 * @param a     time a
 * @param b     time b
 *
 * @returns true if time a < time b.
 */
static inline bool mmosal_time_lt(uint32_t a, uint32_t b)
{
    return ((int32_t)(a - b)) < 0;
}

/**
 * Check if time a is less than or equal to time b, taking into account wrapping.
 *
 * |       a       |       b       | return |
 * |---------------|---------------|--------|
 * |             0 |             0 | true   |
 * |             0 |             1 | true   |
 * |             1 |             0 | false  |
 * | @c 0xffffffff |             0 | true   |
 * |             0 | @c 0xffffffff | false  |
 *
 *
 * @param a     time a
 * @param b     time b
 *
 * @returns true if time a <= time b.
 */
static inline bool mmosal_time_le(uint32_t a, uint32_t b)
{
    return ((int32_t)(a - b)) <= 0;
}

/**
 * Given two times, return the one that is greatest (taking into account wrapping).
 *
 * |      a        |       b       | return |
 * |---------------|---------------|--------|
 * |             0 |             0 |      0 |
 * |             0 |             1 |      1 |
 * |             1 |             0 |      1 |
 * | @c 0xffffffff |             0 |      0 |
 * |             0 | @c 0xffffffff |      0 |
 *
 * @param a     time a
 * @param b     time b
 *
 * @returns a if time a >= time b else b.
 */
static inline uint32_t mmosal_time_max(uint32_t a, uint32_t b)
{
    if (mmosal_time_lt(a, b)) {
        return b;
    } else {
        return a;
    }
}

/**
 * Check if the given time has already passed. i.e the system time is greater than the given time.
 *
 * @param t     The time to check.
 *
 * @return @c true if the given time has passed, else @c false.
 */
static inline bool mmosal_time_has_passed(uint32_t t)
{
    return mmosal_time_le(t, mmosal_get_time_ms());
}

/**
 * @}
 */

/*
 * ---------------------------------------------------------------------------------------------
 */

/**
 * @defgroup MMOSAL_TIMER Timers
 *
 * Provides support for RTOS timers.
 *
 * @note The timer service must have a priority higher than @ref MMOSAL_TASK_PRI_HIGH. This is
 *       because tasks may yield to allow the timer service to execute. In some RTOS implementations
 *       if there are no other tasks at a higher or equal priority to the task that calls @ref
 *       mmosal_task_yield() then the RTOS scheduler will simply select the task that called
 *       @ref mmosal_task_yield() to run again.
 *
 * @{
 */

/** Timer opaque handler data type. */
struct mmosal_timer;

/**
 * Function type definition for timer callbacks.
 *
 * @param timer     The timer that triggered the callback.
 */
typedef void (*timer_callback_t)(struct mmosal_timer *timer);

/**
 * Create a new timer.
 *
 * @param name              The name of the timer.
 * @param timer_period_ms   The period of the timer in milliseconds.
 * @param auto_reload       If @c true then the timer will automatically be reloaded when
 *                          it expires.
 * @param arg               Void pointer that can be used to store a value for the timer callback.
 *                          Pass NULL if unused.
 * @param callback          Callback to be triggered when the timer expires.
 *
 * @returns an opaque handle to the timer, or @c NULL on failure.
 *
 * @warning Ensure that the timer callback function does not block or cause the calling task to
 *          be placed in a blocked state.
 */
struct mmosal_timer *mmosal_timer_create(const char *name, uint32_t timer_period_ms, bool auto_reload, void *arg,
                                         timer_callback_t callback);

/**
 * Delete a timer.
 *
 * @param timer The timer to delete.
 */
void mmosal_timer_delete(struct mmosal_timer *timer);

/**
 * Start a timer.
 *
 * @param timer The timer to start.
 *
 * @returns @c true if the timer was started successfully, else @c false.
 */
bool mmosal_timer_start(struct mmosal_timer *timer);

/**
 * Stop a timer.
 *
 * @param timer The timer to stop.
 *
 * @returns @c true if the timer was stopped successfully, else @c false.
 */
bool mmosal_timer_stop(struct mmosal_timer *timer);

/**
 * Change timer period.
 *
 * @warning May not be invoked from an ISR.
 *
 * @param timer         The timer to assign the new period.
 * @param new_period    Period to assign to the timer.
 *
 * @returns @c true if the timer period was stopped change, else @c false.
 */
bool mmosal_timer_change_period(struct mmosal_timer *timer, uint32_t new_period);

/**
 * Get the opaque argument associated with a given timer.
 *
 * @param timer The timer to retrieve the argument from.
 *
 * @returns void pointer to the argument.
 */
void *mmosal_timer_get_arg(struct mmosal_timer *timer);

/**
 * Queries the timer to determine if it running.
 *
 * @param timer The timer to retrieve the argument from.
 *
 * @returns @c true if the timer is running, else @c false.
 */
bool mmosal_is_timer_active(struct mmosal_timer *timer);

/**
 * @}
 */

/*
 * ---------------------------------------------------------------------------------------------
 */

/**
 * @defgroup MMOSAL_ASSERT Assertions and failure handling
 *
 * Provides support for assertions and logging failure data.
 *
 * @{
 */

#ifndef MMOSAL_FILEID
/** File identifier: if not provided by compiler then define to zero. */
#define MMOSAL_FILEID 0
#endif

/** Data structure used to store information about a failure that can be preserved across reset. */
struct mmosal_failure_info {
    /** Content of the PC register at assertion. */
    uint32_t pc;
    /** Content of the LR register at assertion. */
    uint32_t lr;
    /** File identifier. */
    uint32_t fileid;
    /** Source code line at which the assertion was triggered. */
    uint32_t line;
    /** Arbitrary platform-specific failure info.
     *  Will be zeroes in the case of assertion failure. */
    uint32_t platform_info[4];
};

/**
 * Log failure information in a way that it is preserved across reboots so that it can be
 * available for postmortem analysis.
 *
 * @param info      Failure information to log.
 */
void mmosal_log_failure_info(const struct mmosal_failure_info *info);

#ifdef MMOSAL_NO_DEBUGLOG
/** Empty logging handler. */
#define MMOSAL_LOG_FAILURE_INFO(...)
#else
/** Initialize a mmosal_failure_info struct based on current state and invoke
 *  mmosal_log_failure_info(). */
#define MMOSAL_LOG_FAILURE_INFO(...)                                                                                             \
    do {                                                                                                                         \
        void *pc;                                                                                                                \
        struct mmosal_failure_info info = {                                                                                      \
            .lr = (uint32_t)MMPORT_GET_LR(),                                                                                     \
            .fileid = MMOSAL_FILEID,                                                                                             \
            .line = __LINE__,                                                                                                    \
            .platform_info = {__VA_ARGS__},                                                                                      \
        };                                                                                                                       \
        MMPORT_GET_PC(pc);                                                                                                       \
        info.pc = (uint32_t)pc;                                                                                                  \
        mmosal_log_failure_info(&info);                                                                                          \
    } while (0)
#endif

/**
 * Assertion handler implementation.
 *
 * @note This function does not return.
 */
void mmosal_impl_assert(void);

#ifndef MMOSAL_NOASSERT
/**
 * Assert that the given expression evaluates to true and abort execution if not.
 *
 * @param expr  Expression to evaluation. If it evaluates to @c false then the assertion handler
 *              will be triggered.
 */
#ifndef MMOSAL_ASSERT
#define MMOSAL_ASSERT(expr)                                                                                                      \
    do {                                                                                                                         \
        if (!(expr)) {                                                                                                           \
            MMOSAL_LOG_FAILURE_INFO(0);                                                                                          \
            mmosal_impl_assert();                                                                                                \
            while (1) {                                                                                                          \
            }                                                                                                                    \
        }                                                                                                                        \
    } while (0)
#endif

/**
 * Assert that the given expression evaluates to true and abort execution if not.
 *
 * @param expr  Expression to evaluation. If it evaluates to @c false then the assertion handler
 *              will be triggered.
 * @param ...   Up to 4 32-bit unsigned integers to log.
 */
#ifndef MMOSAL_ASSERT_LOG_DATA
#define MMOSAL_ASSERT_LOG_DATA(expr, ...)                                                                                        \
    do {                                                                                                                         \
        if (!(expr)) {                                                                                                           \
            MMOSAL_LOG_FAILURE_INFO(__VA_ARGS__);                                                                                \
            mmosal_impl_assert();                                                                                                \
            while (1) {                                                                                                          \
            }                                                                                                                    \
        }                                                                                                                        \
    } while (0)
#endif
#else
/** Empty assertion handler. */
#define MMOSAL_ASSERT(expr) (void)(expr)
/** Empty assertion handler. */
#define MMOSAL_ASSERT_LOG_DATA(expre, ...) (void)(expr)
#endif

/** @} */

/**
 * @defgroup MMOSAL_MISC Miscellaneous functions
 *
 * @{
 */

/**
 * A safer version of @c strncpy.
 *
 * Up to `size - 1` bytes will be copied from @p src to @p dst and the result will be
 * null-terminated.
 *
 * @param dst   The buffer to copy the string into.
 * @param src   The source string. This must be null-terminated.
 * @param size  The size of the @p dst buffer.
 *
 * @returns true if the string was truncated else false (note that a size of 0 will always result
 *          this function returning true).
 */
static inline bool mmosal_safer_strcpy(char *dst, const char *src, size_t size)
{
    bool ret;

    if (size == 0) {
        return true;
    }

    strncpy(dst, src, size);

    ret = dst[size - 1] != '\0';
    dst[size - 1] = '\0';
    return ret;
}

/** @} */

#ifdef __cplusplus
}
#endif

/** @} */
