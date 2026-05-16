#ifdef USE_MM_IOT_ESP32
/*
 * Copyright 2024 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "mmbuf.h"
#include "mmosal.h"
#include "mmutils.h"

static const struct mmbuf_ops mmbuf_heap_ops = {.free_mmbuf = mmosal_free};

struct mmbuf *mmbuf_alloc_on_heap(uint32_t space_at_start, uint32_t space_at_end)
{
    struct mmbuf *mmbuf;
    uint8_t *buf;
    uint32_t alloc_len = MM_FAST_ROUND_UP(sizeof(*mmbuf), 4) + MM_FAST_ROUND_UP(space_at_start + space_at_end, 4);
    mmbuf = (struct mmbuf *)mmosal_malloc(alloc_len);
    if (mmbuf == NULL) {
        return NULL;
    }

    /* We zero the buffer as a defensive measure to reduce the likelihood of unintentionally
     * leaking information. */
    memset((uint8_t *)mmbuf, 0, alloc_len);

    buf = ((uint8_t *)mmbuf) + MM_FAST_ROUND_UP(sizeof(*mmbuf), 4);

    mmbuf_init(mmbuf, buf, MM_FAST_ROUND_UP(space_at_start + space_at_end, 4), space_at_start, &mmbuf_heap_ops);

    return mmbuf;
}

struct mmbuf *mmbuf_make_copy_on_heap(struct mmbuf *original)
{
    struct mmbuf *mmbuf;
    uint8_t *buf;
    uint32_t alloc_len = MM_FAST_ROUND_UP(sizeof(*original), 4) + original->buf_len;
    mmbuf = (struct mmbuf *)mmosal_malloc(alloc_len);
    if (mmbuf == NULL) {
        return NULL;
    }

    buf = ((uint8_t *)mmbuf) + MM_FAST_ROUND_UP(sizeof(*mmbuf), 4);
    mmbuf_init(mmbuf, buf, original->buf_len, original->start_offset, &mmbuf_heap_ops);
    mmbuf->data_len = original->data_len;

    if (original->data_len) {
        memcpy(mmbuf_get_data_start(mmbuf), mmbuf_get_data_start(original), mmbuf_get_data_length(original));
    }

    return mmbuf;
}

void mmbuf_release(struct mmbuf *mmbuf)
{
    if (mmbuf == NULL) {
        return;
    }

    MMOSAL_ASSERT(mmbuf->ops != NULL && mmbuf->ops->free_mmbuf != NULL);
    mmbuf->ops->free_mmbuf(mmbuf);
}

#ifdef MMBUF_SANITY
static void mmbuf_list_sanity_check(struct mmbuf_list *list)
{
    unsigned cnt = 0;
    struct mmbuf *walk;
    struct mmbuf *prev = NULL;

    for (walk = list->head; walk != NULL; walk = walk->next) {
        cnt++;
        prev = walk;
    }

    MMOSAL_ASSERT(cnt == list->len);
    MMOSAL_ASSERT(prev == list->tail);
}
#endif

void mmbuf_list_prepend(struct mmbuf_list *list, struct mmbuf *mmbuf)
{
    mmbuf->next = list->head;
    list->head = mmbuf;
    list->len++;

    if (list->tail == NULL) {
        list->tail = list->head;
    }

#ifdef MMBUF_SANITY
    mmbuf_list_sanity_check(list);
#endif
}

void mmbuf_list_append(struct mmbuf_list *list, struct mmbuf *mmbuf)
{
    mmbuf->next = NULL;
    if (list->head == NULL) {
        list->head = mmbuf;
        list->tail = mmbuf;
    } else {
        list->tail->next = mmbuf;
        list->tail = mmbuf;
    }
    list->len++;

#ifdef MMBUF_SANITY
    mmbuf_list_sanity_check(list);
#endif
}

static struct mmbuf *mmbuf_find_prev(struct mmbuf_list *list, struct mmbuf *mmbuf)
{
    struct mmbuf *walk, *next;
    for (walk = list->head, next = walk->next; next != NULL; walk = next, next = walk->next) {
        if (next == mmbuf) {
            return walk;
        }
    }
    return NULL;
}

bool mmbuf_list_remove(struct mmbuf_list *list, struct mmbuf *mmbuf)
{
    struct mmbuf *prev = NULL;

    if (list->head == NULL) {
        return false;
    }

    if (list->head == mmbuf) {
        list->head = mmbuf->next;
    } else {
        prev = mmbuf_find_prev(list, mmbuf);
        if (prev == NULL) {
            return false;
        }
        prev->next = mmbuf->next;
    }

    if (list->tail == mmbuf) {
        list->tail = prev;
    }

    list->len--;
    mmbuf->next = NULL;

#ifdef MMBUF_SANITY
    mmbuf_list_sanity_check(list);
#endif

    return true;
}

struct mmbuf *mmbuf_list_dequeue(struct mmbuf_list *list)
{
    if (list->head == NULL) {
        return NULL;
    } else {
        struct mmbuf *mmbuf = list->head;
        list->head = mmbuf->next;
        list->len--;

        if (list->tail == mmbuf) {
            list->tail = NULL;
        }

#ifdef MMBUF_SANITY
        mmbuf_list_sanity_check(list);
#endif
        if (mmbuf != NULL) {
            mmbuf->next = NULL;
        }
        return mmbuf;
    }
}

struct mmbuf *mmbuf_list_dequeue_tail(struct mmbuf_list *list)
{
    if (list->tail == NULL) {
        return NULL;
    }

    struct mmbuf *mmbuf = list->tail;
    mmbuf_list_remove(list, mmbuf);

    return mmbuf;
}

void mmbuf_list_clear(struct mmbuf_list *list)
{
    struct mmbuf *walk;
    struct mmbuf *next;

#ifdef MMBUF_SANITY
    mmbuf_list_sanity_check(list);
#endif

    if (list->head == NULL) {
        return;
    }

    for (walk = list->head, next = walk->next; walk != NULL; walk = next, next = walk ? walk->next : NULL) {
        mmbuf_release(walk);
    }

    list->len = 0;
    list->head = NULL;
    list->tail = NULL;
}

#endif /* USE_MM_IOT_ESP32 */
