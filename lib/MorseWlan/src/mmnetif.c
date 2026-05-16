#ifdef USE_MM_IOT_ESP32
/*
 * Copyright 2021-2023 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mmnetif.h"
#include "mmosal.h"
#include "mmwlan.h"

#include "lwip/etharp.h"
#include "lwip/ethip6.h"
#include "lwip/tcpip.h"
#if LWIP_SNMP
#include "lwip/snmp.h"
#endif

struct netif_state {
    volatile uint8_t tx_qos_tid;
};

static struct netif_state *get_netif_state(struct netif *netif)
{
    MMOSAL_ASSERT(netif->state != NULL);
    return (struct netif_state *)netif->state;
}

/** pbuf wrapper around an mmpkt. */
struct mmpkt_pbuf_wrapper {
    struct pbuf_custom p;
    struct mmpkt *pkt;
    struct mmpktview *pktview;
};

LWIP_MEMPOOL_DECLARE(RX_POOL, MMPKTMEM_RX_POOL_N_BLOCKS, sizeof(struct mmpkt_pbuf_wrapper), "mmpkt_rx");

static void mmpkt_pbuf_wrapper_free(struct pbuf *p)
{
    struct mmpkt_pbuf_wrapper *pbuf = (struct mmpkt_pbuf_wrapper *)p;
    if (p == NULL) {
        return;
    }
    mmpkt_close(&pbuf->pktview);
    mmpkt_release(pbuf->pkt);
    LWIP_MEMPOOL_FREE(RX_POOL, pbuf);
}

static void mmnetif_rx(struct mmpkt *rxpkt, void *arg)
{
    struct netif *netif = (struct netif *)arg;
    LWIP_ASSERT("arg NULL", netif != NULL);

    LWIP_DEBUGF(NETIF_DEBUG, ("mmnetif: packet received\n"));

    struct mmpkt_pbuf_wrapper *pbuf = (struct mmpkt_pbuf_wrapper *)LWIP_MEMPOOL_ALLOC(RX_POOL);
    if (pbuf != NULL) {
        struct pbuf *p;

        pbuf->p.custom_free_function = mmpkt_pbuf_wrapper_free;
        pbuf->pkt = rxpkt;
        pbuf->pktview = mmpkt_open(pbuf->pkt);

        p = pbuf_alloced_custom(PBUF_RAW, mmpkt_get_data_length(pbuf->pktview), PBUF_REF, &pbuf->p,
                                mmpkt_get_data_start(pbuf->pktview), mmpkt_get_data_length(pbuf->pktview));
        int ret = tcpip_input(p, netif);
        if (ret == ERR_OK) {
            LINK_STATS_INC(link.recv);
        } else {
            LWIP_DEBUGF(NETIF_DEBUG, ("mmnetif: input error\n"));
            pbuf_free(p);
            LINK_STATS_INC(link.memerr);
            LINK_STATS_INC(link.drop);
        }
    } else {
        LWIP_DEBUGF(NETIF_DEBUG | LWIP_DBG_LEVEL_SERIOUS, ("mmnetif: alloc error\n"));
        LINK_STATS_INC(link.memerr);
        mmpkt_release(rxpkt);
    }
}

static void mmnetif_link_state(enum mmwlan_link_state link_state, void *arg)
{
    struct netif *netif = (struct netif *)arg;
    LWIP_ASSERT("arg NULL", netif != NULL);

    LOCK_TCPIP_CORE();
    if (link_state == MMWLAN_LINK_DOWN) {
        LWIP_DEBUGF(NETIF_DEBUG | LWIP_DBG_LEVEL_ALL, ("mmnetif: link down\n"));
        /* Note: we cast netif_set_link_down to tcpip_callback_fn since the tcpip_callback_fn
         * has a "void *" parameter and netif_set_link_down has "struct netif *". */
        err_t err = tcpip_callback_with_block((tcpip_callback_fn)netif_set_link_down, netif, 0);
        LWIP_ASSERT("sched callback failed", err == ERR_OK);
    } else {
        LWIP_DEBUGF(NETIF_DEBUG | LWIP_DBG_LEVEL_ALL, ("mmnetif: link up\n"));
        /* Note: we cast netif_set_link_down to tcpip_callback_fn since the tcpip_callback_fn
         * has a "void *" parameter and netif_set_link_down has "struct netif *". */
        err_t err = tcpip_callback_with_block((tcpip_callback_fn)netif_set_link_up, netif, 0);
        LWIP_ASSERT("sched callback failed", err == ERR_OK);
    }
    UNLOCK_TCPIP_CORE();
}

static err_t mmnetif_tx(struct netif *netif, struct pbuf *p)
{
    struct mmpkt *pkt;
    struct mmpktview *pktview;
    enum mmwlan_status status;
    struct pbuf *walk;
    struct mmwlan_tx_metadata metadata = {
        .tid = get_netif_state(netif)->tx_qos_tid,
    };

    status = mmwlan_tx_wait_until_ready(1000);
    if (status != MMWLAN_SUCCESS) {
        LWIP_DEBUGF(NETIF_DEBUG | LWIP_DBG_LEVEL_SERIOUS, ("mmnetif: transmit blocked\n"));
        LINK_STATS_INC(link.drop);
        return ERR_BUF;
    }

    pkt = mmwlan_alloc_mmpkt_for_tx(p->tot_len, metadata.tid);
    if (pkt == NULL) {
        LWIP_DEBUGF(NETIF_DEBUG | LWIP_DBG_LEVEL_SERIOUS, ("mmnetif: allocation failure\n"));
        LINK_STATS_INC(link.memerr);
        return ERR_MEM;
    }
    pktview = mmpkt_open(pkt);
    for (walk = p; walk != NULL; walk = walk->next) {
        mmpkt_append_data(pktview, (const uint8_t *)walk->payload, walk->len);
    }
    mmpkt_close(&pktview);

    status = mmwlan_tx_pkt(pkt, &metadata);
    if (status != MMWLAN_SUCCESS) {
        LWIP_DEBUGF(NETIF_DEBUG | LWIP_DBG_LEVEL_SERIOUS, ("mmnetif: error sending packet\n"));
        LINK_STATS_INC(link.drop);
        return ERR_BUF;
    }

    LWIP_DEBUGF(NETIF_DEBUG | LWIP_DBG_LEVEL_ALL, ("mmnetif: packet sent\n"));
    LINK_STATS_INC(link.xmit);
    return ERR_OK;
}

err_t mmnetif_init(struct netif *netif)
{
    static bool initialised = false;
    if (initialised) {
        return ERR_IF;
    }

    LWIP_MEMPOOL_INIT(RX_POOL);

    LWIP_DEBUGF(NETIF_DEBUG | LWIP_DBG_LEVEL_ALL, ("mmnetif: initialising mmnetif\n"));

#if LWIP_SNMP
    NETIF_INIT_SNMP(netif, snmp_ifType_ethernet_csmacd, 1000000UL);
#endif

    enum mmwlan_status status;

    /* Boot the transceiver so that we can read the MAC address. */
    struct mmwlan_boot_args boot_args = MMWLAN_BOOT_ARGS_INIT;
    status = mmwlan_boot(&boot_args);
    if (status != MMWLAN_SUCCESS) {
        LWIP_DEBUGF(NETIF_DEBUG | LWIP_DBG_LEVEL_SEVERE, ("mmwlan_boot failed with code %d\n", status));
    }
    MMOSAL_ASSERT(status == MMWLAN_SUCCESS);

    /* Set MAC hardware address */
    netif->hwaddr_len = MMWLAN_MAC_ADDR_LEN;
    status = mmwlan_get_mac_addr(netif->hwaddr);
    MMOSAL_ASSERT(status == MMWLAN_SUCCESS);

    netif->mtu = 1500;
#if LWIP_IPV4 && !LWIP_IPV6
    netif->flags |= NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_IGMP;
#else
    netif->flags |= NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_IGMP | NETIF_FLAG_MLD6;
#endif

    netif->state = NULL;
    netif->name[0] = 'M';
    netif->name[1] = 'M';

#if LWIP_IPV4
    netif->output = etharp_output;
#endif
#if LWIP_IPV6
    netif->output_ip6 = ethip6_output;
#endif
    netif->linkoutput = mmnetif_tx;

    struct netif_state *state = (struct netif_state *)mmosal_malloc(sizeof(*state));
    MMOSAL_ASSERT(state != NULL);
    state->tx_qos_tid = MMWLAN_TX_DEFAULT_QOS_TID;
    netif->state = state;

    status = mmwlan_register_rx_pkt_cb(mmnetif_rx, netif);
    MMOSAL_ASSERT(status == MMWLAN_SUCCESS);
    status = mmwlan_register_link_state_cb(mmnetif_link_state, netif);
    MMOSAL_ASSERT(status == MMWLAN_SUCCESS);

    printf("Morse LwIP interface initialised. MAC address %02x:%02x:%02x:%02x:%02x:%02x\n", netif->hwaddr[0], netif->hwaddr[1],
           netif->hwaddr[2], netif->hwaddr[3], netif->hwaddr[4], netif->hwaddr[5]);

    initialised = true;

    return ERR_OK;
}

void mmnetif_set_tx_qos_tid(struct netif *netif, uint8_t tid)
{
    MMOSAL_ASSERT(tid <= MMWLAN_MAX_QOS_TID);
    get_netif_state(netif)->tx_qos_tid = tid;
}

#endif
