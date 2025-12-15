/**
 * @file MinimalBatchBuffer.cpp
 * @brief Minimal 2-slot RAM buffer implementation (v7.0)
 *
 * NASA Power of 10 compliant implementation with:
 *  - Static allocation only
 *  - 2+ assertions per function
 *  - Fixed loop bounds (max 2)
 *  - Memory barriers for ARM Cortex-M33
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "MinimalBatchBuffer.h"
#include <string.h>
#include <assert.h>

/* Memory barrier for ARM Cortex-M33 (RP2350) */
#if defined(ARDUINO_ARCH_RP2040) || defined(PICO_RP2350)
#include "hardware/sync.h"
#define MEMORY_BARRIER() __dmb()
#else
#define MEMORY_BARRIER() __sync_synchronize()
#endif

/**
 * @brief Static buffer instance (no dynamic allocation)
 * NASA Rule 3: All allocation at compile time
 */
static minimal_batch_buffer_t g_minimal_buffer;

void minimal_buffer_init(void)
{
    /* NASA Rule 5: Two assertions minimum */
    assert(MINIMAL_BUFFER_SLOTS == 2);
    assert(MINIMAL_BUFFER_DATA_SIZE <= 512);

    /* Zero the entire structure */
    memset(&g_minimal_buffer, 0, sizeof(g_minimal_buffer));

    /* Set magic number */
    g_minimal_buffer.header.magic = MINIMAL_BUFFER_MAGIC;
    g_minimal_buffer.header.write_index = 0;
    g_minimal_buffer.header.read_index = 0;
    g_minimal_buffer.header.batch_count = 0;

    /* Memory barrier to ensure visibility to other cores */
    MEMORY_BARRIER();
}

bool minimal_buffer_write(const uint8_t *data, uint16_t length, uint32_t batch_id)
{
    /* NASA Rule 5: Two assertions minimum */
    assert(data != NULL);
    assert(length <= MINIMAL_BUFFER_DATA_SIZE);

    /* Additional validation */
    if (data == NULL || length > MINIMAL_BUFFER_DATA_SIZE) {
        return false;
    }

    /* Check if buffer is full */
    if (g_minimal_buffer.header.batch_count >= MINIMAL_BUFFER_SLOTS) {
        return false;  /* Buffer full - 2 batches already stored */
    }

    /* Get write slot */
    uint8_t slot_index = g_minimal_buffer.header.write_index;

    /* NASA Rule 6: Bounds check */
    assert(slot_index < MINIMAL_BUFFER_SLOTS);

    minimal_batch_slot_t *slot = &g_minimal_buffer.slots[slot_index];

    /* Write batch data */
    memcpy(slot->data, data, length);
    slot->data_length = length;
    slot->batch_id = batch_id;
    slot->flags = 0;

    /* Memory barrier before updating indices */
    MEMORY_BARRIER();

    /* Update write index (circular: 0 → 1 → 0) */
    g_minimal_buffer.header.write_index = (slot_index + 1) % MINIMAL_BUFFER_SLOTS;
    g_minimal_buffer.header.batch_count++;

    /* Memory barrier after updating indices */
    MEMORY_BARRIER();

    return true;
}

bool minimal_buffer_read(uint8_t *buffer, uint16_t max_length,
                         uint16_t *actual_length, uint32_t *batch_id)
{
    /* NASA Rule 5: Two assertions minimum */
    assert(buffer != NULL);
    assert(actual_length != NULL);

    /* Additional assertions */
    assert(batch_id != NULL);
    assert(max_length > 0);

    /* Validate pointers */
    if (buffer == NULL || actual_length == NULL || batch_id == NULL) {
        return false;
    }

    /* Memory barrier before reading indices */
    MEMORY_BARRIER();

    /* Check if buffer is empty */
    if (g_minimal_buffer.header.batch_count == 0) {
        *actual_length = 0;
        *batch_id = 0;
        return false;
    }

    /* Get read slot */
    uint8_t slot_index = g_minimal_buffer.header.read_index;

    /* NASA Rule 6: Bounds check */
    assert(slot_index < MINIMAL_BUFFER_SLOTS);

    const minimal_batch_slot_t *slot = &g_minimal_buffer.slots[slot_index];

    /* Determine how much to copy */
    uint16_t copy_length = slot->data_length;
    if (copy_length > max_length) {
        copy_length = max_length;
    }

    /* Copy data (peek - don't update indices yet) */
    memcpy(buffer, slot->data, copy_length);
    *actual_length = copy_length;
    *batch_id = slot->batch_id;

    return true;
}

bool minimal_buffer_delete(void)
{
    /* NASA Rule 5: Two assertions minimum */
    assert(g_minimal_buffer.header.magic == MINIMAL_BUFFER_MAGIC);
    assert(g_minimal_buffer.header.read_index < MINIMAL_BUFFER_SLOTS);

    /* Memory barrier before reading count */
    MEMORY_BARRIER();

    /* Check if buffer is empty */
    if (g_minimal_buffer.header.batch_count == 0) {
        return false;
    }

    /* Get current slot and clear it */
    uint8_t slot_index = g_minimal_buffer.header.read_index;
    g_minimal_buffer.slots[slot_index].data_length = 0;
    g_minimal_buffer.slots[slot_index].batch_id = 0;

    /* Memory barrier before updating indices */
    MEMORY_BARRIER();

    /* Update read index (circular: 0 → 1 → 0) */
    g_minimal_buffer.header.read_index = (slot_index + 1) % MINIMAL_BUFFER_SLOTS;
    g_minimal_buffer.header.batch_count--;

    /* Memory barrier after updating indices */
    MEMORY_BARRIER();

    return true;
}

bool minimal_buffer_has_data(void)
{
    /* NASA Rule 5: Two assertions minimum */
    assert(g_minimal_buffer.header.magic == MINIMAL_BUFFER_MAGIC);
    assert(g_minimal_buffer.header.batch_count <= MINIMAL_BUFFER_SLOTS);

    /* Memory barrier before reading */
    MEMORY_BARRIER();

    return g_minimal_buffer.header.batch_count > 0;
}

uint8_t minimal_buffer_count(void)
{
    /* NASA Rule 5: Two assertions minimum */
    assert(g_minimal_buffer.header.magic == MINIMAL_BUFFER_MAGIC);
    assert(g_minimal_buffer.header.batch_count <= MINIMAL_BUFFER_SLOTS);

    /* Memory barrier before reading */
    MEMORY_BARRIER();

    return g_minimal_buffer.header.batch_count;
}
