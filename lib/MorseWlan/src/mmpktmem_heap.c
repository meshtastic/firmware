#ifdef USE_MM_IOT_ESP32
/*
 * Copyright 2024 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdatomic.h>
#include <stdint.h>

#include "mmhal.h"
#include "mmosal.h"
#include "mmpkt.h"
#include "mmpkt_list.h"
#include "mmutils.h"

/* MMPKTMEM_TX_POOL_N_BLOCKS and MMPKTMEM_RX_POOL_N_BLOCKS provide an upper bound on the number
 * of packets we will allocate in the transmit and receive directions respectively. */

#ifndef MMPKTMEM_TX_POOL_N_BLOCKS
#error MMPKTMEM_TX_POOL_N_BLOCKS not defined
#endif

#ifndef MMPKTMEM_RX_POOL_N_BLOCKS
#error MMPKTMEM_RX_POOL_N_BLOCKS not defined
#endif

/* Packet pool for data/management frames configuration. */
#define TX_DATA_POOL_UNPAUSE_THRESHOLD (MMPKTMEM_TX_POOL_N_BLOCKS - 2)
#define TX_DATA_POOL_PAUSE_THRESHOLD (MMPKTMEM_TX_POOL_N_BLOCKS - 1)

/* Packet pool for commands configuration. */
#define TX_COMMAND_POOL_BLOCK_SIZE (256)
#define TX_COMMAND_POOL_N_BLOCKS (2)

#ifndef MMPKT_LOG
#define MMPKT_LOG(...) printf(__VA_ARGS__)
#endif

struct pktmem_data {
    /** Count of allocated tx packets (excluding command pool -- see below). */
    volatile atomic_int_least32_t tx_data_pool_allocated;
    /** Boolean tracking whether the data path is currently paused. */
    volatile atomic_uint_fast8_t tx_data_pool_tx_paused;
    /** Count of allocated rx packets. */
    volatile atomic_int_least32_t rx_pool_allocated;

    /** Command pool free (unallocated) packet list. */
    struct mmpkt_list tx_command_pool_free_list;
    /** Statically allocated memory for the command pool. */
    uint8_t tx_command_pool[TX_COMMAND_POOL_BLOCK_SIZE * TX_COMMAND_POOL_N_BLOCKS];

    /** Flow control callback function pointer. */
    mmhal_wlan_pktmem_tx_flow_control_cb_t tx_flow_control_cb;
};

static struct pktmem_data pktmem;

void mmhal_wlan_pktmem_init(struct mmhal_wlan_pktmem_init_args *args)
{
    unsigned ii;

    memset(&pktmem, 0, sizeof(pktmem));

    pktmem.tx_flow_control_cb = args->tx_flow_control_cb;

    /* Initialize the free (unallocated) packet list of the transmit command pool. */
    for (ii = 0; ii < TX_COMMAND_POOL_N_BLOCKS; ii++) {
        size_t offset = TX_COMMAND_POOL_BLOCK_SIZE * ii;
        mmpkt_list_append(&pktmem.tx_command_pool_free_list, (struct mmpkt *)(pktmem.tx_command_pool + offset));
    }
}

void mmhal_wlan_pktmem_deinit(void)
{
    size_t ii;

    /* If there is still memory allocated, allow some time for other threads to clean up. */
    for (ii = 0; ii < 100; ii++) {
        if ((pktmem.tx_command_pool_free_list.len | pktmem.tx_data_pool_allocated | pktmem.tx_data_pool_allocated) == 0) {
            break;
        }
        mmosal_task_sleep(10);
    }

    /* Check for memory leaks. */
    if (pktmem.tx_data_pool_allocated != 0) {
        MMPKT_LOG("Potential memory leak: %d %s pool allocations at deinit\n", (int)pktmem.tx_data_pool_allocated, "data");
    }

    if (pktmem.tx_command_pool_free_list.len != TX_COMMAND_POOL_N_BLOCKS) {
        MMPKT_LOG("Potential memory leak: %d %s pool allocations at deinit\n",
                  TX_COMMAND_POOL_N_BLOCKS - (int)pktmem.tx_command_pool_free_list.len, "command");
    }
}

/*
 * --------------------------------------------------------------------------------------
 *     Command pool
 * --------------------------------------------------------------------------------------
 */

static void tx_command_reserved_free(void *mmpkt)
{
    struct mmpkt *pkt = (struct mmpkt *)mmpkt;
    MMOSAL_TASK_ENTER_CRITICAL();
    mmpkt_list_append(&pktmem.tx_command_pool_free_list, pkt);
    MMOSAL_TASK_EXIT_CRITICAL();
}

static const struct mmpkt_ops tx_command_pool_ops = {
    .free_mmpkt = tx_command_reserved_free,
};

static struct mmpkt *alloc_pkt_from_list(struct mmpkt_list *list, uint32_t pktbufsize, uint32_t space_at_start,
                                         uint32_t space_at_end, uint32_t metadata_length)
{
    struct mmpkt *mmpkt_buf;
    struct mmpkt *mmpkt;

    MMOSAL_TASK_ENTER_CRITICAL();
    mmpkt_buf = mmpkt_list_dequeue(list);
    MMOSAL_TASK_EXIT_CRITICAL();

    if (mmpkt_buf == NULL) {
        return NULL;
    }

    mmpkt = mmpkt_init_buf((uint8_t *)mmpkt_buf, pktbufsize, space_at_start, space_at_end, metadata_length, &tx_command_pool_ops);
    if (mmpkt == NULL) {
        /* Command was too big for the reserved buffer. Return the reserved buffer. */
        tx_command_reserved_free(mmpkt_buf);
    }

    return mmpkt;
}

static struct mmpkt *command_pool_alloc(uint32_t space_at_start, uint32_t space_at_end, uint32_t metadata_length)
{
    return alloc_pkt_from_list(&pktmem.tx_command_pool_free_list, TX_COMMAND_POOL_BLOCK_SIZE, space_at_start, space_at_end,
                               metadata_length);
}

/*
 * --------------------------------------------------------------------------------------
 *     Data pool
 * --------------------------------------------------------------------------------------
 */

static void tx_data_pool_pkt_free(void *mmpkt)
{
    atomic_int_least32_t old_value = atomic_fetch_sub(&pktmem.tx_data_pool_allocated, 1);
    MMOSAL_ASSERT(old_value > 0);
    mmosal_free(mmpkt);

    if (pktmem.tx_data_pool_allocated < TX_DATA_POOL_UNPAUSE_THRESHOLD) {
        atomic_uint_fast8_t old_tx_paused = atomic_exchange(&pktmem.tx_data_pool_tx_paused, 0);
        if (old_tx_paused) {
            pktmem.tx_flow_control_cb(MMWLAN_TX_READY);
        }
    }
}

static const struct mmpkt_ops tx_data_pool_pkt_ops = {
    .free_mmpkt = tx_data_pool_pkt_free,
};

struct mmpkt *mmhal_wlan_alloc_mmpkt_for_tx(uint8_t pkt_class, uint32_t space_at_start, uint32_t space_at_end,
                                            uint32_t metadata_length)
{
    atomic_int_least32_t old_value;
    struct mmpkt *mmpkt;

    /* For command packets, try allocating from the command pool first. If that fails then
     * we proceed to allocate from the data pool. */
    if (pkt_class == MMHAL_WLAN_PKT_COMMAND) {
        mmpkt = command_pool_alloc(space_at_start, space_at_end, metadata_length);
        if (mmpkt != NULL) {
            return mmpkt;
        }
    }

    old_value = atomic_fetch_add(&pktmem.tx_data_pool_allocated, 1);

    if (old_value >= MMPKTMEM_TX_POOL_N_BLOCKS) {
        /* Maximum allocations reached. Do not attempt to increase further. */
        atomic_fetch_sub(&pktmem.tx_data_pool_allocated, 1);
        return NULL;
    }

    mmpkt = mmpkt_alloc_on_heap(space_at_start, space_at_end, metadata_length);
    if (mmpkt == NULL) {
        atomic_fetch_sub(&pktmem.tx_data_pool_allocated, 1);
        return NULL;
    }

    mmpkt->ops = &tx_data_pool_pkt_ops;

    if (pktmem.tx_data_pool_allocated > TX_DATA_POOL_PAUSE_THRESHOLD) {
        atomic_uint_fast8_t old_tx_paused = atomic_exchange(&pktmem.tx_data_pool_tx_paused, 1);
        if (!old_tx_paused) {
            pktmem.tx_flow_control_cb(MMWLAN_TX_PAUSED);
        }
    }

    return mmpkt;
}

static void rx_pkt_free(void *mmpkt)
{
    if (mmpkt != NULL) {
        atomic_fetch_sub(&pktmem.rx_pool_allocated, 1);
        mmosal_free(mmpkt);
    }
}

static const struct mmpkt_ops mmpkt_rx_ops = {.free_mmpkt = rx_pkt_free};

struct mmpkt *mmhal_wlan_alloc_mmpkt_for_rx(uint32_t capacity, uint32_t metadata_length)
{
    atomic_int_least32_t old_value;
    struct mmpkt *mmpkt;

    old_value = atomic_fetch_add(&pktmem.rx_pool_allocated, 1);

    if (old_value >= MMPKTMEM_RX_POOL_N_BLOCKS) {
        /* Maximum allocations reached. Do not attempt to increase further. */
        atomic_fetch_sub(&pktmem.rx_pool_allocated, 1);
        return NULL;
    }

    /* For now we do not put an explicit limit on the of packets buffers on the RX path. */
    mmpkt = mmpkt_alloc_on_heap(0, capacity, metadata_length);
    if (mmpkt == NULL) {
        atomic_fetch_sub(&pktmem.rx_pool_allocated, 1);
        return NULL;
    }

    /* Override packet ops to use a custom free function that also decrements the
     * allocation count. */
    mmpkt->ops = &mmpkt_rx_ops;
    return mmpkt;
}

#endif
