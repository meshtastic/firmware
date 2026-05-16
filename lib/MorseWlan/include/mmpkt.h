/*
 * Copyright 2022-2024 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @defgroup MMPKT Morse Micro Packet Buffer (mmpkt) API
 *
 * This API provides support for buffers tailored towards packets.
 *
 * @{
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mmosal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Round @p x up to the next multiple of @p m (where @p m is a power of 2).
 *
 * @warning @p m must be a power of 2.
 */
#ifndef MM_FAST_ROUND_UP
#define MM_FAST_ROUND_UP(x, m) ((((x)-1) | ((m)-1)) + 1)
#endif

struct mmdrv_cmd_metadata;
struct mmdrv_tx_metadata;
struct mmdrv_rx_metadata;
struct mmpkt_ops;

/**
 * Union of pointer types for mmpkt metadata.
 *
 * The metadata is accessed through one of these pointers, depending on which context the packet
 * is being used in.
 */
union mmpkt_metadata_ptr {
    /** Opaque pointer for contexts which are unaware of the specific metadata structure. */
    void *opaque;
    /** Metadata for a packet which is being transmitted. */
    struct mmdrv_tx_metadata *tx;
    /** Metadata for a packet which is being received. */
    struct mmdrv_rx_metadata *rx;
    /** Control block for a command response sent to the host. */
    struct mmdrv_cmd_metadata *cmd;
};

/**
 * Core mmpkt data structure.
 *
 * @note The contents of this data structure should never need to be accessed directly. Rather
 *       the various functions provided as part of this API should be used.
 *
 * @code
 *      +----------------------------------------------------------+--------------+
 *      | RESERVED |              Data            |    RESERVED    |   METADATA   |
 *      +----------------------------------------------------------+--------------+
 *      ^          ^                              ^                ^
 *      |          |                              |                |
 *      |          |<-----------data_len--------->|                |
 *      |     start_offset                                         |
 *      |                                                          |
 *      |<-----------------------buf_len-------------------------->|
 *     buf
 * @endcode
 */
struct mmpkt {
    /** The buffer where data is stored. */
    uint8_t *buf;
    /** Length of the buffer. */
    uint32_t buf_len;
    /** Offset where actual data starts in the buffer. */
    uint32_t start_offset;
    /** Length of actual data in the buffer. */
    uint32_t data_len;
    /** Packet metadata used by driver (context dependent). */
    union mmpkt_metadata_ptr metadata;
    /** Reference to operations data structure for this mmpkt. */
    const struct mmpkt_ops *ops;
    /** Pointer that can be used to construct linked lists. */
    struct mmpkt *volatile next;
};

/** Operations data structure for mmpkt. */
struct mmpkt_ops {
    /** Free the given mmpkt. */
    void (*free_mmpkt)(void *mmpkt);
};

/**
 * Opened view of an mmpkt.
 *
 * In this implementation, this structure does not actually exist. We only use it as a pointer type
 * which is incompatible with @ref mmpkt, to distinguish between functions which operate on opened
 * packet views versus functions which can operate on unopened packets.
 *
 * In other implementations of this API, packets must be "opened" (mapped into memory) before their
 * contents can be accessed. Thus the distinction between opened and unopened packets is important
 * for those implementations.
 */
struct mmpktview;

/**
 * Initialize an mmpkt header with the given values.
 *
 * @param mmpkt                 mmpkt to initialize.
 * @param buf                   Pointer to buffer.
 * @param buf_len               Length of @p buf.
 * @param data_start_offset     Initial value for @c start_offset.
 * @param ops                   Operations data structure.
 */
static inline void mmpkt_init(struct mmpkt *mmpkt, uint8_t *buf, uint32_t buf_len, uint32_t data_start_offset,
                              const struct mmpkt_ops *ops)
{
    memset(mmpkt, 0, sizeof(*mmpkt));
    mmpkt->buf = buf;
    mmpkt->buf_len = buf_len;
    mmpkt->start_offset = data_start_offset;
    mmpkt->ops = ops;
}

/**
 * Initialize an mmpkt in a single buffer using the given values.
 *
 * @param buf                   Pointer to buffer.
 * @param buf_len               Length of @p buf.
 * @param space_at_start        Amount of space to reserve at start of buffer.
 * @param space_at_end          Amount of space to reserve at end of buffer.
 * @param metadata_size         Size of metadata (0 for no metadata).
 *
 * @param ops                   Operations data structure.
 *
 * @note @p buf_len must be large enough to contain the @c mmkpt header, the data buffer
 *          (rounded up to the nearest 4 bytes) and metadata (rounded up to the nearest 4 bytes).
 *
 * @returns a pointer to the initialized @c mmpkt (will be the same address as @p buf) or @c NULL
 *          on error (@p buf length too short).
 */
static inline struct mmpkt *mmpkt_init_buf(uint8_t *buf, uint32_t buf_len, uint32_t space_at_start, uint32_t space_at_end,
                                           uint32_t metadata_size, const struct mmpkt_ops *ops)
{
    struct mmpkt *mmpkt = (struct mmpkt *)buf;

    uint8_t *data_start;
    uint32_t header_size = MM_FAST_ROUND_UP(sizeof(*mmpkt), 4);
    uint32_t data_len = MM_FAST_ROUND_UP(space_at_start + space_at_end, 4);
    metadata_size = MM_FAST_ROUND_UP(metadata_size, 4);

    if (header_size + data_len + metadata_size > buf_len) {
        return NULL;
    }

    data_start = ((uint8_t *)mmpkt) + header_size;

    mmpkt_init(mmpkt, data_start, data_len, space_at_start, ops);

    if (metadata_size != 0) {
        mmpkt->metadata.opaque = data_start + data_len;
        memset(mmpkt->metadata.opaque, 0, metadata_size);
    }

    return mmpkt;
}

/**
 * Allocate a new mmpkt on the heap (using @ref mmosal_malloc()).
 *
 * @param space_at_start    Amount of space to reserve at start of buffer.
 * @param space_at_end      Amount of space to reserve at end of buffer.
 * @param metadata_size     Size of metadata (0 for no metadata).
 *
 * @note @c start_offset will be set to @p space_at_start, and @c buf_len will be the sum
 *       of @p space_at_start and @p space_at_end (rounded up to a multiple of 4).
 *
 * @returns newly allocated mmpkt on success or @c NULL on failure.
 */
struct mmpkt *mmpkt_alloc_on_heap(uint32_t space_at_start, uint32_t space_at_end, uint32_t metadata_size);

/**
 * Release a reference to the given mmpkt. If this was the last reference (@c addition_ref_cnt
 * was 0) then the mmpkt will be freed using the appropriate op callback.
 *
 * @param mmpkt     The mmpkt to release reference to. May be @c NULL.
 */
void mmpkt_release(struct mmpkt *mmpkt);

/**
 * Open a view of the given mmpkt.
 *
 * Packets must be opened before the contents of their buffer can be accessed. Most of the
 * functions below expect to be passed an opened view of a packet to operate on.
 *
 * The view must be closed by calling @ref mmpkt_close() before the packet is released by
 * @ref mmpkt_release().
 *
 * @param mmpkt  The mmpkt to be opened.
 *
 * @returns a pointer representing the opened view.
 */
static inline struct mmpktview *mmpkt_open(struct mmpkt *mmpkt)
{
    return (struct mmpktview *)mmpkt;
}

/**
 * Close the given view.
 *
 * @param[in,out] view  Pointer to a variable holding the view to be closed.
 *                      This will be modified to indicate it is no longer valid.
 */
static inline void mmpkt_close(struct mmpktview **view)
{
    (void)(view);
}

/**
 * Get the underlying mmpkt from an opened view.
 *
 * @param view  View of an mmpkt.
 *
 * @returns the underlying mmpkt.
 */
static inline struct mmpkt *mmpkt_from_view(struct mmpktview *view)
{
    return (struct mmpkt *)view;
}

/**
 * Gets a pointer to the start of the data in the mmpkt.
 *
 * @param view      The opened mmpkt to operate on.
 *
 * @returns a pointer to the start of the data in the mmpkt.
 */
static inline uint8_t *mmpkt_get_data_start(struct mmpktview *view)
{
    struct mmpkt *mmpkt = (struct mmpkt *)view;
    return mmpkt->buf + mmpkt->start_offset;
}

/**
 * Gets a pointer to the end of the data in the mmpkt.
 *
 * @param view      The opened mmpkt to operate on.
 *
 * @returns a pointer to the end of the data in the mmpkt.
 */
static inline uint8_t *mmpkt_get_data_end(struct mmpktview *view)
{
    struct mmpkt *mmpkt = (struct mmpkt *)view;
    return mmpkt->buf + mmpkt->start_offset + mmpkt->data_len;
}

/**
 * Peek the length of the data currently from an unopened mmpkt.
 *
 * @param mmpkt      The unopened mmpkt to operate on.
 *
 * @returns the length of the data currently in the mmpkt (note that this is different from the
 *          length of the available buffer space).
 */
static inline uint32_t mmpkt_peek_data_length(struct mmpkt *mmpkt)
{
    return mmpkt->data_len;
}

/**
 * Gets the length of the data currently in the mmpkt.
 *
 * @param view      The opened mmpkt to operate on.
 *
 * @returns the length of the data currently in the mmpkt (note that this is different from the
 *          length of the available buffer space).
 */
static inline uint32_t mmpkt_get_data_length(struct mmpktview *view)
{
    struct mmpkt *mmpkt = (struct mmpkt *)view;
    return mmpkt->data_len;
}

/**
 * Returns the amount of space available for prepending to the data in the buffer.
 *
 * @param view      The opened mmpkt to operate on.
 *
 * @returns the available space in bytes.
 */
static inline uint32_t mmpkt_available_space_at_start(struct mmpktview *view)
{
    struct mmpkt *mmpkt = (struct mmpkt *)view;
    return mmpkt->start_offset;
}

/**
 * Returns the amount of space available for appending to the data in the buffer.
 *
 * @param view      The opened mmpkt to operate on.
 *
 * @returns the available space in bytes.
 */
static inline uint32_t mmpkt_available_space_at_end(struct mmpktview *view)
{
    struct mmpkt *mmpkt = (struct mmpkt *)view;
    return mmpkt->buf_len - (mmpkt->start_offset + mmpkt->data_len);
}

/**
 * Reserves space immediately before the data currently in the given mmpkt and returns
 * a pointer to this space.
 *
 * For a function that also copies data in, see @ref mmpkt_prepend_data().
 *
 * @warning @p len must be less than or equal to @ref mmpkt_available_space_at_start().
 *
 * @param view      The opened mmpkt to operate on.
 * @param len       Length of data to be prepended.
 *
 * @returns a pointer to the place in the buffer where the data should be put.
 */
static inline uint8_t *mmpkt_prepend(struct mmpktview *view, uint32_t len)
{
    struct mmpkt *mmpkt = (struct mmpkt *)view;
    MMOSAL_ASSERT(len <= mmpkt_available_space_at_start(view));
    mmpkt->start_offset -= len;
    mmpkt->data_len += len;
    return mmpkt->buf + mmpkt->start_offset;
}

/**
 * Prepends the given data to the data already in the mmpkt.
 *
 * @warning @p len must be less than or equal to @ref mmpkt_available_space_at_start().
 *
 * @warning The memory area pointed to by data must not overlap with the mmpkt data.
 *
 * @param view      The opened mmpkt to operate on.
 * @param data      The data to be prepended.
 * @param len       Length of data to be prepended.
 */
static inline void mmpkt_prepend_data(struct mmpktview *view, const uint8_t *data, uint32_t len)
{
    uint8_t *dest = mmpkt_prepend(view, len);
    memcpy(dest, data, len);
}

/**
 * Reserves space immediately after the data currently in the given mmpkt and returns
 * a pointer to this space.
 *
 * For a function that also copies data in, see @ref mmpkt_append_data().
 *
 * @warning @p len must be less than or equal to @ref mmpkt_available_space_at_end().
 *
 * @param view      The opened mmpkt to operate on.
 * @param len       Length of data to be append.
 *
 * @returns a pointer to the place in the buffer where the data should be put.
 */
static inline uint8_t *mmpkt_append(struct mmpktview *view, uint32_t len)
{
    struct mmpkt *mmpkt = (struct mmpkt *)view;
    uint8_t *ret = mmpkt_get_data_end(view);
    MMOSAL_ASSERT(len <= mmpkt_available_space_at_end(view));
    mmpkt->data_len += len;
    return ret;
}

/**
 * Appends the given data to the data already in the mmpkt.
 *
 * @warning @p len must be less than or equal to @ref mmpkt_available_space_at_start().
 *
 * @param view      The opened mmpkt to operate on.
 * @param data      The data to be prepended.
 * @param len       Length of data to be prepended.
 */
static inline void mmpkt_append_data(struct mmpktview *view, const uint8_t *data, uint32_t len)
{
    uint8_t *dest = mmpkt_append(view, len);
    memcpy(dest, data, len);
}

/**
 * Retrieve a reference to the metadata associated with the given mmpkt.
 *
 * @param mmpkt     The mmpkt to operate on.
 *
 * @returns a reference to the mmpkt metadata.
 */
static inline union mmpkt_metadata_ptr mmpkt_get_metadata(struct mmpkt *mmpkt)
{
    return mmpkt->metadata;
}

/**
 * Remove data from the start of the mmpkt.
 *
 * @param view      The opened mmpkt to operate on.
 * @param len       Length of data to remove.
 *
 * @returns a pointer to the removed data or NULL if the mmpkt data length was less than @p len.
 */
static inline uint8_t *mmpkt_remove_from_start(struct mmpktview *view, uint32_t len)
{
    struct mmpkt *mmpkt = (struct mmpkt *)view;
    uint8_t *ret;

    if (mmpkt_get_data_length(view) < len) {
        return NULL;
    }

    ret = mmpkt_get_data_start(view);

    mmpkt->start_offset += len;
    mmpkt->data_len -= len;

    return ret;
}

/**
 * Remove data from the end of the mmpkt.
 *
 * @param view      The opened mmpkt to operate on.
 * @param len       Length of data to remove.
 *
 * @returns a pointer to the removed data or NULL if the mmpkt data length was less than @p len.
 */
static inline uint8_t *mmpkt_remove_from_end(struct mmpktview *view, uint32_t len)
{
    struct mmpkt *mmpkt = (struct mmpkt *)view;
    uint8_t *ret;

    if (mmpkt_get_data_length(view) < len) {
        return NULL;
    }

    ret = mmpkt_get_data_end(view) - len;

    mmpkt->data_len -= len;

    return ret;
}

/**
 * Truncate the mmpkt data to the given length.
 *
 * @param mmpkt     mmpkt to operate on.
 * @param len       New data length. (Must be less than or equal to the data length
 *                  of the mmpkt).
 */
static inline void mmpkt_truncate(struct mmpkt *mmpkt, uint32_t len)
{
    MMOSAL_ASSERT(len <= mmpkt->data_len);
    mmpkt->data_len = len;
}

/**
 * Get the `next` pointer embedded in the mmpkt.
 *
 * Used by the mmpkt_list structure for making linked lists of mmpkts.
 *
 * @param mmpkt     The mmpkt to operate on.
 *
 * @returns pointer to the next mmpkt in the chain (may be NULL).
 */
static inline struct mmpkt *mmpkt_get_next(struct mmpkt *mmpkt)
{
    return mmpkt->next;
}

/**
 * Set the `next` pointer embedded in the mmpkt.
 *
 * Used by the mmpkt_list structure for making linked lists of mmpkts.
 *
 * @param mmpkt     The mmpkt to operate on.
 * @param next      The next mmpkt in the chain.
 */
static inline void mmpkt_set_next(struct mmpkt *mmpkt, struct mmpkt *next)
{
    mmpkt->next = next;
}

/**
 * Check whether the given pointer is pointing inside the mmpkt's buffer.
 *
 * @note This checks against the full buffer, which includes any unused regions at the beginning and
 * end of the buffer which do not contain valid packet data.
 *
 * @param view      The opened mmpkt to operate on.
 * @param ptr       The pointer to check.
 *
 * @returns true if the pointer points into the buffer.
 */
static inline bool mmpkt_contains_ptr(struct mmpktview *view, const void *ptr)
{
    struct mmpkt *mmpkt = (struct mmpkt *)view;
    return ((const uint8_t *)ptr >= &mmpkt->buf[0] && (const uint8_t *)ptr < &mmpkt->buf[mmpkt->buf_len]);
}

#ifdef __cplusplus
}
#endif

/** @} */
