/*
 * Copyright 2021-2023 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "lwip/err.h"
#include "lwip/netif.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initializer for the Morse Micro network interface */
err_t mmnetif_init(struct netif *netif);

/**
 * Configure the QoS TID for the @c netif. QoS data will be sent using this TID.
 *
 * @param netif The @c netif to configure.
 * @param tid   The TID value to set.
 */
void mmnetif_set_tx_qos_tid(struct netif *netif, uint8_t tid);

#ifdef __cplusplus
}
#endif
