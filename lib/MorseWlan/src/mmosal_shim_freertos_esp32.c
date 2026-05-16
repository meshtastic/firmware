#ifdef USE_MM_IOT_ESP32
/*
 * Copyright 2021-2023 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_debug_helpers.h"
#include "esp_private/startup_internal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "rom/ets_sys.h"

#include "mmhal.h"
#include "mmosal.h"

/* --------------------------------------------------------------------------------------------- */

/** Maximum number of failure records to store (must be a power of 2). */
#define MAX_FAILURE_RECORDS 4

/** Fast implementation of _x % _m where _m is a power of 2. */
#define FAST_MOD(_x, _m) ((_x) & ((_m)-1))

/** Duration to delay before resetting the device on assert. */
#define DELAY_BEFORE_RESET_MS 1000

/** Data structure for assertion information to be preserved. */
struct mmosal_preserved_failure_info {
    /** Magic number, to check if the info is valid. */
    uint32_t magic;

    /** Number of failures recorded. */
    uint32_t failure_count;

    /** Number of most recently displayed failure. */
    uint32_t displayed_failure_count;

    /** Preserved information from the most recent failure(s). */
    struct mmosal_failure_info info[MAX_FAILURE_RECORDS];
};

/** Magic number to put in @c mmosal_assert_info.magic to indicate that the assertion info
 *  is valid. */
#define ASSERT_INFO_MAGIC (0xabcd1234)

/* Persistent assertion info. Linker script should put this into memory that is not
 * zeroed on boot. Be careful to update linker script if renaming. */
struct mmosal_preserved_failure_info preserved_failure_info __attribute__((section(".noinit")));

void mmosal_log_failure_info(const struct mmosal_failure_info *info)
{
    uint32_t record_num;

    if (preserved_failure_info.magic != ASSERT_INFO_MAGIC) {
        preserved_failure_info.failure_count = 0;
        preserved_failure_info.displayed_failure_count = 0;
    }

    preserved_failure_info.magic = ASSERT_INFO_MAGIC;
    record_num = FAST_MOD(preserved_failure_info.failure_count, MAX_FAILURE_RECORDS);
    preserved_failure_info.failure_count++;
    memcpy(&preserved_failure_info.info[record_num], info, sizeof(*info));
}

static void mmosal_dump_failure_info(void)
{
    unsigned first_failure_num = preserved_failure_info.displayed_failure_count;
    unsigned new_failure_count = preserved_failure_info.failure_count - preserved_failure_info.displayed_failure_count;
    unsigned failure_offset;

    if (new_failure_count >= MAX_FAILURE_RECORDS) {
        first_failure_num = FAST_MOD(preserved_failure_info.failure_count, MAX_FAILURE_RECORDS);
        new_failure_count = MAX_FAILURE_RECORDS;
    }

    for (failure_offset = 0; failure_offset < new_failure_count; failure_offset++) {
        unsigned ii;
        unsigned idx = FAST_MOD(first_failure_num + failure_offset, MAX_FAILURE_RECORDS);
        struct mmosal_failure_info *info = &preserved_failure_info.info[idx];

        ets_printf("Failure %u logged at pc 0x%08lx, lr 0x%08lx, line %ld in %08lx\n", first_failure_num + failure_offset,
                   info->pc, info->lr, info->line, info->fileid);

        for (ii = 0; ii < sizeof(info->platform_info) / sizeof(info->platform_info[0]); ii++) {
            ets_printf("    0x%08lx\n", info->platform_info[ii]);
        }
    }

    preserved_failure_info.displayed_failure_count = preserved_failure_info.failure_count;
}

void mmosal_impl_assert(void)
{
    ets_printf("MMOSAL Assert, CPU %d (current core) backtrace", xPortGetCoreID());
    (void)esp_backtrace_print(100);
#ifdef HALT_ON_ASSERT
    if (preserved_failure_info.magic == ASSERT_INFO_MAGIC) {
        mmosal_dump_failure_info();
    }
    mmosal_disable_interrupts();
    mmhal_log_flush();
    MMPORT_BREAKPOINT();
#else
    mmosal_task_sleep(DELAY_BEFORE_RESET_MS);
    mmhal_reset();
#endif
    while (1) {
    }
}

/* Function to be called as part of the secondary initialization. See [System
 * Initialization](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/startup.html#system-initialization)
 * for more information. */
ESP_SYSTEM_INIT_FN(mmosal_dump_failure_info, BIT(0), 999)
{
    if (preserved_failure_info.magic == ASSERT_INFO_MAGIC) {
        mmosal_dump_failure_info();
    }
    return ESP_OK;
}

/* --------------------------------------------------------------------------------------------- */

void *mmosal_malloc_(size_t size)
{
    return pvPortMalloc(size);
}

#ifdef MMOSAL_TRACK_ALLOCATIONS
void *mmosal_malloc_dbg(size_t size, const char *name, unsigned line_number)
{
    return pvPortMalloc_dbg(size, name, line_number);
}
#else
void *mmosal_malloc_dbg(size_t size, const char *name, unsigned line_number)
{
    (void)name;
    (void)line_number;
    return pvPortMalloc(size);
}
#endif

void mmosal_free(void *p)
{
    vPortFree(p);
}

void *mmosal_realloc(void *ptr, size_t size)
{
    return realloc(ptr, size);
}

void *mmosal_calloc(size_t nitems, size_t size)
{
    void *ptr = pvPortMalloc(nitems * size);
    if (ptr == NULL) {
        return NULL;
    }

    memset(ptr, 0, nitems * size);
    return ptr;
}

/* --------------------------------------------------------------------------------------------- */

struct mmosal_task_arg {
    mmosal_task_fn_t task_fn;
    void *task_fn_arg;
};

void mmosal_task_main(void *arg)
{
    struct mmosal_task_arg task_arg = *(struct mmosal_task_arg *)arg;
    mmosal_free(arg);
    task_arg.task_fn(task_arg.task_fn_arg);
    mmosal_task_delete(NULL);
}

struct mmosal_task *mmosal_task_create(mmosal_task_fn_t task_fn, void *argument, enum mmosal_task_priority priority,
                                       unsigned stack_size_u32, const char *name)
{
    TaskHandle_t handle;
    UBaseType_t freertos_priority = tskIDLE_PRIORITY + priority;

    struct mmosal_task_arg *task_arg = (struct mmosal_task_arg *)mmosal_malloc(sizeof(*task_arg));
    if (task_arg == NULL) {
        return NULL;
    }
    task_arg->task_fn = task_fn;
    task_arg->task_fn_arg = argument;

    BaseType_t result = xTaskCreate(mmosal_task_main, name, stack_size_u32 * 4, task_arg, freertos_priority, &handle);
    if (result == pdFAIL) {
        mmosal_free(task_arg);
        return NULL;
    }

    return (struct mmosal_task *)handle;
}

void mmosal_task_delete(struct mmosal_task *task)
{
    vTaskDelete((TaskHandle_t)task);
}

/*
 * Warning: this function should not be used since eTaskGetState() is not a reliable
 * means of testing whether a task has completed.
 *
 * This function will be removed in future.
 */
void mmosal_task_join(struct mmosal_task *task)
{
    while (eTaskGetState((TaskHandle_t)task) != eDeleted) {
        mmosal_task_sleep(10);
    }
}

struct mmosal_task *mmosal_task_get_active(void)
{
    return (struct mmosal_task *)xTaskGetCurrentTaskHandle();
}

void mmosal_task_yield(void)
{
    taskYIELD();
}

void mmosal_task_sleep(uint32_t duration_ms)
{
    vTaskDelay(duration_ms / portTICK_PERIOD_MS);
}

static portMUX_TYPE task_spinlock = portMUX_INITIALIZER_UNLOCKED;

void mmosal_task_enter_critical(void)
{
    taskENTER_CRITICAL(&task_spinlock);
}

void mmosal_task_exit_critical(void)
{
    taskEXIT_CRITICAL(&task_spinlock);
}

void mmosal_disable_interrupts(void)
{
    taskDISABLE_INTERRUPTS();
}

void mmosal_enable_interrupts(void)
{
    taskENABLE_INTERRUPTS();
}

const char *mmosal_task_name(void)
{
    TaskHandle_t t = xTaskGetCurrentTaskHandle();
    return pcTaskGetName(t);
}

bool mmosal_task_wait_for_notification(uint32_t timeout_ms)
{
    TickType_t wait = portMAX_DELAY;
    if (timeout_ms < UINT32_MAX) {
        wait = pdMS_TO_TICKS(timeout_ms);
    }
    uint32_t ret = ulTaskNotifyTake(pdTRUE, /* Act as binary semaphore */
                                    wait);
    return (ret != 0);
}

void mmosal_task_notify(struct mmosal_task *task)
{
    xTaskNotifyGive((TaskHandle_t)task);
}

void mmosal_task_notify_from_isr(struct mmosal_task *task)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    vTaskNotifyGiveFromISR((TaskHandle_t)task, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

/* --------------------------------------------------------------------------------------------- */

struct mmosal_mutex *mmosal_mutex_create(const char *name)
{
    struct mmosal_mutex *mutex = (struct mmosal_mutex *)xSemaphoreCreateMutex();
#if (configUSE_TRACE_FACILITY == 1) && defined(ENABLE_TRACEALYZER) && ENABLE_TRACEALYZER
    if (name != NULL) {
        vTraceSetMutexName(mutex, name);
    }
#else
    (void)name;
#endif
    return mutex;
}

void mmosal_mutex_delete(struct mmosal_mutex *mutex)
{
    if (mutex != NULL) {
        vQueueDelete((SemaphoreHandle_t)mutex);
    }
}

bool mmosal_mutex_get(struct mmosal_mutex *mutex, uint32_t timeout_ms)
{
    uint32_t timeout_ticks = portMAX_DELAY;
    if (timeout_ms != UINT32_MAX) {
        timeout_ticks = timeout_ms / portTICK_PERIOD_MS;
    }
    return (xSemaphoreTake((SemaphoreHandle_t)mutex, timeout_ticks) == pdPASS);
}

bool mmosal_mutex_release(struct mmosal_mutex *mutex)
{
    return (xSemaphoreGive((SemaphoreHandle_t)mutex) == pdPASS);
}

bool mmosal_mutex_is_held_by_active_task(struct mmosal_mutex *mutex)
{
    return xSemaphoreGetMutexHolder((SemaphoreHandle_t)mutex) == xTaskGetCurrentTaskHandle();
}

/* --------------------------------------------------------------------------------------------- */

struct mmosal_sem *mmosal_sem_create(unsigned max_count, unsigned initial_count, const char *name)
{
    struct mmosal_sem *sem = (struct mmosal_sem *)xSemaphoreCreateCounting(max_count, initial_count);
#if (configUSE_TRACE_FACILITY == 1) && defined(ENABLE_TRACEALYZER) && ENABLE_TRACEALYZER
    if (name != NULL) {
        vTraceSetSemaphoreName(sem, name);
    }
#else
    (void)name;
#endif
    return sem;
}

void mmosal_sem_delete(struct mmosal_sem *sem)
{
    vQueueDelete((SemaphoreHandle_t)sem);
}

bool mmosal_sem_give(struct mmosal_sem *sem)
{
    return xSemaphoreGive((SemaphoreHandle_t)sem);
}

bool mmosal_sem_give_from_isr(struct mmosal_sem *sem)
{
    BaseType_t task_woken = false;
    BaseType_t ret = xSemaphoreGiveFromISR((SemaphoreHandle_t)sem, &task_woken);
    if (ret == pdPASS) {
        portYIELD_FROM_ISR(task_woken);
        return true;
    } else {
        return false;
    }
}

bool mmosal_sem_wait(struct mmosal_sem *sem, uint32_t timeout_ms)
{
    uint32_t timeout_ticks = portMAX_DELAY;
    if (timeout_ms != UINT32_MAX) {
        timeout_ticks = timeout_ms / portTICK_PERIOD_MS;
    }
    return (xSemaphoreTake((SemaphoreHandle_t)sem, timeout_ticks) == pdPASS);
}

uint32_t mmosal_sem_get_count(struct mmosal_sem *sem)
{
    return uxSemaphoreGetCount((SemaphoreHandle_t)sem);
}

/* --------------------------------------------------------------------------------------------- */

struct mmosal_semb *mmosal_semb_create(const char *name)
{
    struct mmosal_semb *semb = (struct mmosal_semb *)xSemaphoreCreateBinary();
#if (configUSE_TRACE_FACILITY == 1) && defined(ENABLE_TRACEALYZER) && ENABLE_TRACEALYZER
    if (name != NULL) {
        vTraceSetSemaphoreName(semb, name);
    }
#else
    (void)name;
#endif
    return semb;
}

void mmosal_semb_delete(struct mmosal_semb *semb)
{
    vQueueDelete((SemaphoreHandle_t)semb);
}

bool mmosal_semb_give(struct mmosal_semb *semb)
{
    return (xSemaphoreGive((SemaphoreHandle_t)semb) == pdPASS);
}

bool mmosal_semb_give_from_isr(struct mmosal_semb *semb)
{
    BaseType_t task_woken = pdFALSE;
    BaseType_t ret = xSemaphoreGiveFromISR((SemaphoreHandle_t)semb, &task_woken);
    if (ret == pdPASS) {
        portYIELD_FROM_ISR(task_woken);
        return true;
    } else {
        return false;
    }
}

bool mmosal_semb_wait(struct mmosal_semb *semb, uint32_t timeout_ms)
{
    uint32_t timeout_ticks = portMAX_DELAY;
    if (timeout_ms != UINT32_MAX) {
        timeout_ticks = timeout_ms / portTICK_PERIOD_MS;
    }
    return (xSemaphoreTake((SemaphoreHandle_t)semb, timeout_ticks) == pdPASS);
}

/* --------------------------------------------------------------------------------------------- */

struct mmosal_queue *mmosal_queue_create(size_t num_items, size_t item_size, const char *name)
{
    struct mmosal_queue *queue = (struct mmosal_queue *)xQueueCreate(num_items, item_size);
#if (configUSE_TRACE_FACILITY == 1) && defined(ENABLE_TRACEALYZER) && ENABLE_TRACEALYZER
    if (name != NULL) {
        vTraceSetQueueName(queue, name);
    }
#else
    (void)name;
#endif
    return queue;
}

void mmosal_queue_delete(struct mmosal_queue *queue)
{
    vQueueDelete((SemaphoreHandle_t)queue);
}

bool mmosal_queue_pop(struct mmosal_queue *queue, void *item, uint32_t timeout_ms)
{
    uint32_t timeout_ticks = portMAX_DELAY;
    if (timeout_ms != UINT32_MAX) {
        timeout_ticks = timeout_ms / portTICK_PERIOD_MS;
    }
    return (xQueueReceive((SemaphoreHandle_t)queue, item, timeout_ticks) == pdPASS);
}

bool mmosal_queue_push(struct mmosal_queue *queue, const void *item, uint32_t timeout_ms)
{
    uint32_t timeout_ticks = portMAX_DELAY;
    if (timeout_ms != UINT32_MAX) {
        timeout_ticks = timeout_ms / portTICK_PERIOD_MS;
    }
    return (xQueueSendToBack((SemaphoreHandle_t)queue, item, timeout_ticks) == pdPASS);
}

bool mmosal_queue_pop_from_isr(struct mmosal_queue *queue, void *item)
{
    BaseType_t task_woken = pdFALSE;
    if (xQueueReceiveFromISR((SemaphoreHandle_t)queue, item, &task_woken) == pdTRUE) {
        portYIELD_FROM_ISR(task_woken);
        return true;
    } else {
        return false;
    }
}

bool mmosal_queue_push_from_isr(struct mmosal_queue *queue, const void *item)
{
    BaseType_t task_woken = pdFALSE;
    if (xQueueSendToBackFromISR((SemaphoreHandle_t)queue, item, &task_woken) == pdTRUE) {
        portYIELD_FROM_ISR(task_woken);
        return true;
    } else {
        return false;
    }
}

/* --------------------------------------------------------------------------------------------- */

uint32_t mmosal_get_time_ms(void)
{
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

uint32_t mmosal_get_time_ticks(void)
{
    return xTaskGetTickCount();
}

uint32_t mmosal_ticks_per_second(void)
{
    return portTICK_PERIOD_MS * 1000;
}

/* --------------------------------------------------------------------------------------------- */

struct mmosal_timer *mmosal_timer_create(const char *name, uint32_t timer_period, bool auto_reload, void *arg,
                                         timer_callback_t callback)
{
    /*
     * The software timer callback functions execute in the context of a task that is
     * created automatically when the FreeRTOS scheduler is started. Therefore, it is essential that
     * software timer callback functions never call FreeRTOS API functions that will result in the
     * calling task entering the Blocked state. It is ok to call functions such as xQueueReceive(), but
     * only if the function’s xTicksToWait parameter (which specifies the function’s block time) is set
     * to 0. It is not ok to call functions such as vTaskDelay(), as calling vTaskDelay() will always
     * place the calling task into the Blocked state.
     */
    return (struct mmosal_timer *)xTimerCreate(name, pdMS_TO_TICKS(timer_period), (UBaseType_t)auto_reload, arg,
                                               (TimerCallbackFunction_t)callback);
}

void mmosal_timer_delete(struct mmosal_timer *timer)
{
    if (timer != NULL) {
        BaseType_t ret = xTimerDelete((TimerHandle_t)timer, 0);
        configASSERT(ret == pdPASS);
    }
}

bool mmosal_timer_start(struct mmosal_timer *timer)
{
    BaseType_t ret = xTimerStart((TimerHandle_t)timer, 0);

    return (ret == pdPASS);
}

bool mmosal_timer_stop(struct mmosal_timer *timer)
{
    BaseType_t ret = xTimerStop((TimerHandle_t)timer, 0);

    return (ret == pdPASS);
}

bool mmosal_timer_change_period(struct mmosal_timer *timer, uint32_t new_period)
{
    BaseType_t ret = xTimerChangePeriod((TimerHandle_t)timer, pdMS_TO_TICKS(new_period), 0);

    return (ret == pdPASS);
}

void *mmosal_timer_get_arg(struct mmosal_timer *timer)
{
    return pvTimerGetTimerID((TimerHandle_t)timer);
}

bool mmosal_is_timer_active(struct mmosal_timer *timer)
{
    BaseType_t ret = xTimerIsTimerActive((TimerHandle_t)timer);

    return (ret != pdFALSE);
}

#endif /* USE_MM_IOT_ESP32 */
