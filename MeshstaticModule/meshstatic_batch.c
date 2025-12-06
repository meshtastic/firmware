/**
 * @file meshstatic_batch.c
 * @brief Implementation of CSV-based keystroke batch manager
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "meshstatic_batch.h"
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * Constants
 * ============================================================================ */

/** CSV header line (exactly 42 bytes including newline) */
static const char CSV_HEADER[] = "timestamp_us,scancode,modifier,character\n";

/* ============================================================================
 * Private Helper Functions
 * ============================================================================ */

/**
 * @brief Append CSV row to batch buffer
 *
 * Formats keystroke as CSV row and appends to csv_buffer.
 * Format: "timestamp_us,scancode,modifier,character\n"
 * Example: "1234567890,0x04,0x00,a\n"
 *
 * @param batch Pointer to batch structure
 * @param keystroke Pointer to keystroke record
 * @return Number of bytes written, or 0 on error
 */
static uint32_t append_csv_row(meshstatic_batch_t* batch,
                               const meshstatic_keystroke_t* keystroke)
{
    /* Check if we have space left in CSV buffer */
    uint32_t remaining = MESHSTATIC_CSV_BUFFER_SIZE - batch->meta.csv_length;

    if (remaining < MESHSTATIC_MAX_CSV_LINE_LENGTH) {
        return 0;  /* Not enough space */
    }

    /* Format CSV row */
    char row[MESHSTATIC_MAX_CSV_LINE_LENGTH];
    int written = snprintf(row, sizeof(row), "%lu,0x%02X,0x%02X,%c\n",
                          (unsigned long)keystroke->timestamp_us,
                          keystroke->scancode,
                          keystroke->modifier,
                          keystroke->character);

    /* Check if formatting succeeded */
    if (written < 0 || written >= (int)sizeof(row)) {
        return 0;  /* Format error */
    }

    /* Append row to CSV buffer */
    char* append_pos = batch->csv_buffer + batch->meta.csv_length;
    strcpy(append_pos, row);

    /* Update CSV length */
    batch->meta.csv_length += written;

    return written;
}

/**
 * @brief Check if adding keystroke would exceed 200-byte limit
 *
 * @param batch Pointer to batch structure
 * @return true if batch would exceed limit
 */
static bool would_exceed_limit(const meshstatic_batch_t* batch)
{
    /* Estimate size of next CSV row: ~25 bytes average */
    uint32_t estimated_next_row = 25;
    uint32_t total_after_add = batch->meta.csv_length + estimated_next_row;

    return (total_after_add > MESHSTATIC_MAX_BATCH_SIZE);
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

void meshstatic_batch_init(meshstatic_batch_t* batch)
{
    if (!batch) return;

    /* Clear entire structure */
    memset(batch, 0, sizeof(meshstatic_batch_t));

    /* Set initial batch ID (starts at 1) */
    batch->meta.batch_id = 1;

    /* Initialize CSV buffer with header */
    strcpy(batch->csv_buffer, CSV_HEADER);
    batch->meta.csv_length = strlen(CSV_HEADER);

    /* Mark as not needing flush yet */
    batch->meta.needs_flush = false;
}

bool meshstatic_batch_add(meshstatic_batch_t* batch,
                          uint8_t scancode,
                          uint8_t modifier,
                          char character,
                          uint32_t timestamp_us)
{
    if (!batch) return false;

    /* Check if batch is already full */
    if (batch->meta.count >= MESHSTATIC_MAX_KEYSTROKES_PER_BATCH) {
        batch->meta.needs_flush = true;
        return false;  /* Batch full, cannot add more */
    }

    /* Check if adding this keystroke would exceed 200-byte limit */
    if (would_exceed_limit(batch)) {
        batch->meta.needs_flush = true;
        return false;  /* Would exceed limit */
    }

    /* Add keystroke to array */
    uint32_t idx = batch->meta.count;
    batch->keystrokes[idx].timestamp_us = timestamp_us;
    batch->keystrokes[idx].scancode = scancode;
    batch->keystrokes[idx].modifier = modifier;
    batch->keystrokes[idx].character = character;

    /* Append CSV row */
    uint32_t bytes_written = append_csv_row(batch, &batch->keystrokes[idx]);
    if (bytes_written == 0) {
        /* Failed to append - mark as full */
        batch->meta.needs_flush = true;
        return false;
    }

    /* Update metadata */
    batch->meta.count++;

    /* Update timing metadata */
    if (batch->meta.count == 1) {
        batch->meta.start_time_us = timestamp_us;
    }
    batch->meta.end_time_us = timestamp_us;

    /* Check if batch reached limit after adding */
    if (batch->meta.csv_length >= (MESHSTATIC_MAX_BATCH_SIZE - MESHSTATIC_MAX_CSV_LINE_LENGTH)) {
        batch->meta.needs_flush = true;
    }

    return true;
}

bool meshstatic_batch_is_full(const meshstatic_batch_t* batch)
{
    if (!batch) return false;

    return batch->meta.needs_flush;
}

const char* meshstatic_batch_get_csv(const meshstatic_batch_t* batch)
{
    if (!batch) return NULL;

    return batch->csv_buffer;
}

uint32_t meshstatic_batch_get_csv_length(const meshstatic_batch_t* batch)
{
    if (!batch) return 0;

    return batch->meta.csv_length;
}

void meshstatic_batch_reset(meshstatic_batch_t* batch)
{
    if (!batch) return;

    /* Save batch ID before reset */
    uint32_t next_batch_id = batch->meta.batch_id + 1;

    /* Clear entire structure */
    memset(batch, 0, sizeof(meshstatic_batch_t));

    /* Restore incremented batch ID */
    batch->meta.batch_id = next_batch_id;

    /* Reinitialize CSV buffer with header */
    strcpy(batch->csv_buffer, CSV_HEADER);
    batch->meta.csv_length = strlen(CSV_HEADER);

    /* Mark as not needing flush */
    batch->meta.needs_flush = false;
}

void meshstatic_batch_get_stats(const meshstatic_batch_t* batch,
                                uint32_t* count,
                                uint32_t* csv_length,
                                uint32_t* batch_id)
{
    if (!batch) return;

    if (count) *count = batch->meta.count;
    if (csv_length) *csv_length = batch->meta.csv_length;
    if (batch_id) *batch_id = batch->meta.batch_id;
}
