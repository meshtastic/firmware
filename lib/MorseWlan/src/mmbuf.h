/*
 * Copyright 2024 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @defgroup MMBUF Morse Micro Buffer (mmbuf) API
 *
 * This API provides support for buffers tailored towards packet-like data that has
 * headers and trailers that are applied at subsequent layers.
 *
 * It is designed to support various backends for memory allocation. The default
 * is allocation on the heap, but other methods could be used due to the flexible API.
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

struct mmbuf_ops;

/**
 * Core mmbuf data structure.
 *
 * @note The contents of this data structure should never need to be accessed directly. Rather
 *       the various functions provided as part of this API should be used.
 *
 * @code
 *      +----------------------------------------------------------+
 *      | RESERVED |              Data            |    RESERVED    |
 *      +----------------------------------------------------------+
 *      ^          ^                              ^                ^
 *      |          |                              |                |
 *      |          |<-----------data_len--------->|                |
 *      |     start_offset                                         |
 *      |                                                          |
 *      |<-----------------------buf_len-------------------------->|
 *     buf
 * @endcode
 */
struct mmbuf {
    /** The buffer where data is stored. */
    uint8_t *buf;
    /** Length of the buffer. */
    uint32_t buf_len;
    /** Offset where actual data starts in the buffer. */
    uint32_t start_offset;
    /** Length of actual data in the buffer. */
    uint32_t data_len;
    /** Reference to operations data structure for this mmbuf. */
    const struct mmbuf_ops *ops;
    /** Pointer that can be used to construct linked lists. */
    struct mmbuf *volatile next;
};

/** Operations data structure for mmbuf. */
struct mmbuf_ops {
    /** Free the given mmbuf. */
    void (*free_mmbuf)(void *mmbuf);
};

/**
 * Initialize an mmbuf header with the given values.
 *
 * @param mmbuf                 mmbuf to initialize.
 * @param buf                   Pointer to buffer.
 * @param buf_len               Length of @p buf.
 * @param data_start_offset     Initial value for @c start_offset.
 * @param ops                   Operations data structure.
 */
static inline void mmbuf_init(struct mmbuf *mmbuf, uint8_t *buf, uint32_t buf_len, uint32_t data_start_offset,
                              const struct mmbuf_ops *ops)
{
    memset(mmbuf, 0, sizeof(*mmbuf));
    mmbuf->buf = buf;
    mmbuf->buf_len = buf_len;
    mmbuf->start_offset = data_start_offset;
    mmbuf->ops = ops;
}

/**
 * Allocate a new mmbuf on the heap (using @ref mmosal_malloc()).
 *
 * @param space_at_start    Amount of space to reserve at start of buffer.
 * @param space_at_end      Amount of space to reserve at end of buffer.
 *
 * @note @c start_offset will be set to @p space_at_start, and @c buf_len will be the sum
 *       of @p space_at_start and @p space_at_end (rounded up to a multiple of 4).
 *
 * @returns newly allocated mmbuf on success or @c NULL on failure.
 */
struct mmbuf *mmbuf_alloc_on_heap(uint32_t space_at_start, uint32_t space_at_end);

/**
 * Make a copy of the given mmbuf. Note that regardless of the backend that allocated
 * @p original, the newly allocated mmbuf will be allocated on the heap using
 * @ref mmbuf_alloc_on_heap().
 *
 * @param original          mmbuf to copy.
 *
 * @returns newly allocated mmbuf on success or @c NULL on failure.
 */
struct mmbuf *mmbuf_make_copy_on_heap(struct mmbuf *original);

/**
 * Release a reference to the given mmbuf. If this was the last reference (@c addition_ref_cnt
 * was 0) then the mmbuf will be freed using the appropriate op callback.
 *
 * @param mmbuf     The mmbuf to release reference to. May be @c NULL.
 */
void mmbuf_release(struct mmbuf *mmbuf);

/**
 * Gets a pointer to the start of the data in the mmbuf.
 *
 * @param mmbuf     The mmbuf to operate on.
 *
 * @returns a pointer to the start of the data in the mmbuf.
 */
static inline uint8_t *mmbuf_get_data_start(struct mmbuf *mmbuf)
{
    return mmbuf->buf + mmbuf->start_offset;
}

/**
 * Gets a pointer to the end of the data in the mmbuf.
 *
 * @param mmbuf     The mmbuf to operate on.
 *
 * @returns a pointer to the end of the data in the mmbuf.
 */
static inline uint8_t *mmbuf_get_data_end(struct mmbuf *mmbuf)
{
    return mmbuf->buf + mmbuf->start_offset + mmbuf->data_len;
}

/**
 * Gets the length of the data currently in the mmbuf.
 *
 * @param mmbuf     The mmbuf to operate on.
 *
 * @returns the length of the data currently in the mmbuf (note that this is different from the
 *          length of the available buffer space).
 */
static inline uint32_t mmbuf_get_data_length(struct mmbuf *mmbuf)
{
    return mmbuf->data_len;
}

/**
 * Returns the amount of space available for prepending to the data in the buffer.
 *
 * @param mmbuf     The mmbuf to operate on.
 *
 * @returns the available space in bytes.
 */
static inline uint32_t mmbuf_available_space_at_start(struct mmbuf *mmbuf)
{
    return mmbuf->start_offset;
}

/**
 * Returns the amount of space available for appending to the data in the buffer.
 *
 * @param mmbuf     The mmbuf to operate on.
 *
 * @returns the available space in bytes.
 */
static inline uint32_t mmbuf_available_space_at_end(struct mmbuf *mmbuf)
{
    return mmbuf->buf_len - (mmbuf->start_offset + mmbuf->data_len);
}

/**
 * Reserves space immediately before the data currently in the given mmbuf and returns
 * a pointer to this space.
 *
 * For a function that also copies data in, see @ref mmbuf_prepend_data().
 *
 * @warning @p len must be less than or equal to @ref mmbuf_available_space_at_start().
 *
 * @param mmbuf     The mmbuf to operate on.
 * @param len       Length of data to be prepended.
 *
 * @returns a pointer to the place in the buffer where the data should be put.
 */
static inline uint8_t *mmbuf_prepend(struct mmbuf *mmbuf, uint32_t len)
{
    MMOSAL_ASSERT(len <= mmbuf_available_space_at_start(mmbuf));
    mmbuf->start_offset -= len;
    mmbuf->data_len += len;
    return mmbuf->buf + mmbuf->start_offset;
}

/**
 * Prepends the given data to the data already in the mmbuf.
 *
 * @warning @p len must be less than or equal to @ref mmbuf_available_space_at_start().
 *
 * @warning The memory area pointed to by data must not overlap with the mmbuf data.
 *
 * @param mmbuf     The mmbuf to operate on.
 * @param data      The data to be prepended.
 * @param len       Length of data to be prepended.
 */
static inline void mmbuf_prepend_data(struct mmbuf *mmbuf, const uint8_t *data, uint32_t len)
{
    uint8_t *dest = mmbuf_prepend(mmbuf, len);
    memcpy(dest, data, len);
}

/**
 * Reserves space immediately after the data currently in the given mmbuf and returns
 * a pointer to this space.
 *
 * For a function that also copies data in, see @ref mmbuf_append_data().
 *
 * @warning @p len must be less than or equal to @ref mmbuf_available_space_at_end().
 *
 * @param mmbuf     The mmbuf to operate on.
 * @param len       Length of data to be append.
 *
 * @returns a pointer to the place in the buffer where the data should be put.
 */
static inline uint8_t *mmbuf_append(struct mmbuf *mmbuf, uint32_t len)
{
    uint8_t *ret = mmbuf_get_data_end(mmbuf);
    MMOSAL_ASSERT(len <= mmbuf_available_space_at_end(mmbuf));
    mmbuf->data_len += len;
    return ret;
}

/**
 * Appends the given data to the data already in the mmbuf.
 *
 * @warning @p len must be less than or equal to @ref mmbuf_available_space_at_start().
 *
 * @param mmbuf     The mmbuf to operate on.
 * @param data      The data to be prepended.
 * @param len       Length of data to be prepended.
 */
static inline void mmbuf_append_data(struct mmbuf *mmbuf, const uint8_t *data, uint32_t len)
{
    uint8_t *dest = mmbuf_append(mmbuf, len);
    memcpy(dest, data, len);
}

/**
 * Remove data from the start of the mmbuf.
 *
 * @param mmbuf     mmbuf to operate on.
 * @param len       Length of data to remove.
 *
 * @returns a pointer to the removed data or NULL if the mmbuf data length was less than @p len.
 */
static inline uint8_t *mmbuf_remove_from_start(struct mmbuf *mmbuf, uint32_t len)
{
    uint8_t *ret;

    if (mmbuf_get_data_length(mmbuf) < len) {
        return NULL;
    }

    ret = mmbuf_get_data_start(mmbuf);

    mmbuf->start_offset += len;
    mmbuf->data_len -= len;

    return ret;
}

/**
 * Remove data from the end of the mmbuf.
 *
 * @param mmbuf     mmbuf to operate on.
 * @param len       Length of data to remove.
 *
 * @returns a pointer to the removed data or NULL if the mmbuf data length was less than @p len.
 */
static inline uint8_t *mmbuf_remove_from_end(struct mmbuf *mmbuf, uint32_t len)
{
    uint8_t *ret;

    if (mmbuf_get_data_length(mmbuf) < len) {
        return NULL;
    }

    ret = mmbuf_get_data_end(mmbuf) - len;

    mmbuf->data_len -= len;

    return ret;
}

/**
 * Truncate the mmbuf data to the given length.
 *
 * @param mmbuf     mmbuf to operate on.
 * @param len       New data length. (Must be less than or equal to the data length
 *                  of the mmbuf).
 */
static inline void mmbuf_truncate(struct mmbuf *mmbuf, uint32_t len)
{
    MMOSAL_ASSERT(len <= mmbuf->data_len);
    mmbuf->data_len = len;
}

/* --------------------------------------------------------------------------------------------- */

/** Structure that can be used as the head of a linked list of mmbufs that counts its length. */
struct mmbuf_list {
    /** First mmbuf in the list. */
    struct mmbuf *volatile head;
    /** Last mmbuf in the list. */
    struct mmbuf *volatile tail;
    /** Length of the list. */
    volatile uint32_t len;
};

/** Static initializer for @ref mmbuf_list. */
#define MMBUF_LIST_INIT                                                                                                          \
    {                                                                                                                            \
        NULL, NULL, 0                                                                                                            \
    }

/**
 * Initialization function for @ref mmbuf_list, for cases where @c MMBUF_LIST_INIT
 * cannot be used.
 *
 * @param list  The mmbuf_list to init.
 */
static inline void mmbuf_list_init(struct mmbuf_list *list)
{
    list->head = NULL;
    list->tail = NULL;
    list->len = 0;
}

/**
 * Add an mmbuf to the start of an mmbuf list.
 *
 * @param list  The list to prepend to.
 * @param mmbuf The mmbuf to prepend.
 */
void mmbuf_list_prepend(struct mmbuf_list *list, struct mmbuf *mmbuf);

/**
 * Add an mmbuf to the end of an mmbuf list.
 *
 * @param list  The list to append to.
 * @param mmbuf The mmbuf to append.
 */
void mmbuf_list_append(struct mmbuf_list *list, struct mmbuf *mmbuf);

/**
 * Remove an mmbuf from an mmbuf list.
 *
 * @param list  The list to remove from.
 * @param mmbuf The mmbuf to remove.
 *
 * @returns @c true if the given @c mmbuf was present in @c list, else @c fase.
 */
bool mmbuf_list_remove(struct mmbuf_list *list, struct mmbuf *mmbuf);

/**
 * Remove the mmbuf at the head of the list and return it.
 *
 * @param list  The list to dequeue from.
 *
 * @returns the dequeued mmbuf, or @c NULL if the list is empty.
 */
struct mmbuf *mmbuf_list_dequeue(struct mmbuf_list *list);

/**
 * Remove the mmbuf at the tail of the list and return it.
 *
 * @param list  The list to dequeue from.
 *
 * @returns the dequeued mmbuf, or @c NULL if the list is empty.
 */
struct mmbuf *mmbuf_list_dequeue_tail(struct mmbuf_list *list);

/**
 * Remove all mmbufs from the list and return as a linked list.
 *
 * @param list  The list to dequeue from.
 *
 * @returns the dequeued mmbufs, or @c NULL if the list is empty.
 */
static inline struct mmbuf *mmbuf_list_dequeue_all(struct mmbuf_list *list)
{
    struct mmbuf *head = list->head;
    list->head = NULL;
    list->tail = NULL;
    list->len = 0;
    return head;
}

/**
 * Checks whether the given mmbuf list is empty.
 *
 * @param list  The list to check.
 *
 * @returns @c true if the list is empty, else @c false.
 */
static inline bool mmbuf_list_is_empty(struct mmbuf_list *list)
{
    return (list->head == NULL);
}

/**
 * Returns the head of the mmbuf list.
 *
 * @param list  The list to peek into.
 *
 * @returns the mmbuf at the head of the list.
 */
static inline struct mmbuf *mmbuf_list_peek(struct mmbuf_list *list)
{
    return list->head;
}

/**
 * Returns the tail of the mmbuf list.
 *
 * @param list  The list to peek into.
 *
 * @returns the mmbuf at the tail of the list.
 */
static inline struct mmbuf *mmbuf_list_peek_tail(struct mmbuf_list *list)
{
    return list->tail;
}

/**
 * Free all the packets in the given list and reset the list to empty state.
 *
 * @param list  The list to clear.
 */
void mmbuf_list_clear(struct mmbuf_list *list);

#ifdef __cplusplus
}
#endif

/** @} */
