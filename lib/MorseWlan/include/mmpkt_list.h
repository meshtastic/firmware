/*
 * Copyright 2022-2024 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @ingroup MMPKT
 *
 * @{
 */

#pragma once

#include <stddef.h>

#include "mmpkt.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Structure that can be used as the head of a linked list of mmpkts that counts its length. */
struct mmpkt_list {
    /** First mmpkt in the list. */
    struct mmpkt *volatile head;
    /** Last mmpkt in the list. */
    struct mmpkt *volatile tail;
    /** Length of the list. */
    volatile uint32_t len;
};

/** Static initializer for @ref mmpkt_list. */
#define MMPKT_LIST_INIT                                                                                                          \
    {                                                                                                                            \
        NULL, NULL, 0                                                                                                            \
    }

/**
 * Initialization function for @ref mmpkt_list, for cases where @c MMPKT_LIST_INIT
 * cannot be used.
 *
 * @param list The mmpkt_list to init.
 */
static inline void mmpkt_list_init(struct mmpkt_list *list)
{
    list->head = NULL;
    list->tail = NULL;
    list->len = 0;
}

/**
 * Add an mmpkt to the start of an mmpkt list.
 *
 * @param list  The list to prepend to.
 * @param mmpkt The mmpkt to prepend.
 */
void mmpkt_list_prepend(struct mmpkt_list *list, struct mmpkt *mmpkt);

/**
 * Add an mmpkt to the end of an mmpkt list.
 *
 * @param list  The list to append to.
 * @param mmpkt The mmpkt to append.
 */
void mmpkt_list_append(struct mmpkt_list *list, struct mmpkt *mmpkt);

/**
 * Remove an mmpkt from an mmpkt list.
 *
 * @param list  The list to remove from.
 * @param mmpkt The mmpkt to remove.
 */
void mmpkt_list_remove(struct mmpkt_list *list, struct mmpkt *mmpkt);

/**
 * Remove the mmpkt at the head of the list and return it.
 *
 * @param list The list to dequeue from.
 *
 * @returns the dequeued mmpkt, or @c NULL if the list is empty.
 */
struct mmpkt *mmpkt_list_dequeue(struct mmpkt_list *list);

/**
 * Remove the mmpkt at the tail of the list and return it.
 *
 * @param list The list to dequeue from.
 *
 * @returns the dequeued mmpkt, or @c NULL if the list is empty.
 */
struct mmpkt *mmpkt_list_dequeue_tail(struct mmpkt_list *list);

/**
 * Remove all mmpkts from the list and return as a linked list.
 *
 * @param list The list to dequeue from.
 *
 * @returns the dequeued mmpkts, or @c NULL if the list is empty.
 */
static inline struct mmpkt *mmpkt_list_dequeue_all(struct mmpkt_list *list)
{
    struct mmpkt *head = list->head;
    list->head = NULL;
    list->tail = NULL;
    list->len = 0;
    return head;
}

/**
 * Checks whether the given mmpkt list is empty.
 *
 * @param list The list to check.
 *
 * @returns @c true if the list is empty, else @c false.
 */
static inline bool mmpkt_list_is_empty(struct mmpkt_list *list)
{
    return (list->head == NULL);
}

/**
 * Returns the head of the mmpkt list.
 *
 * @param list The list to peek into.
 *
 * @returns the mmpkt at the head of the list.
 */
static inline struct mmpkt *mmpkt_list_peek(struct mmpkt_list *list)
{
    return list->head;
}

/**
 * Returns the tail of the mmpkt list.
 *
 * @param list The list to peek into.
 *
 * @returns the mmpkt at the tail of the list.
 */
static inline struct mmpkt *mmpkt_list_peek_tail(struct mmpkt_list *list)
{
    return list->tail;
}

/**
 * Free all the packets in the given list and reset the list to empty state.
 *
 * @param list The list to clear.
 */
void mmpkt_list_clear(struct mmpkt_list *list);

/**
 * Safely walk the mmpkt list.
 *
 * @warning This macro cannot be used following an if statement with no parentheses if there
 *          is an else clause. For example, do not do:
 *          `if (x) MMPKT_LIST_WALK(a,b,c) else foo();` -- instead:
 *          `if (x) { MMPKT_LIST_WALK(a,b,c) } else foo();`
 */
#define MMPKT_LIST_WALK(_lst, _wlk, _nxt)                                                                                        \
    if ((_lst)->head != NULL) /* NOLINT(readability/braces) */                                                                   \
        for (_wlk = (_lst)->head, _nxt = mmpkt_get_next(_wlk); _wlk != NULL;                                                     \
             _wlk = _nxt, _nxt = _wlk ? mmpkt_get_next(_wlk) : NULL)

#ifdef __cplusplus
}
#endif

/**
 * @}
 */
