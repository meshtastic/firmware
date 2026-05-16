#ifdef USE_MM_IOT_ESP32
/*
 * Stubs for LWIP netif callback API that Arduino-ESP32's prebuilt LWIP omits
 * (CONFIG_LWIP_NETIF_STATUS_CALLBACK / _LINK_CALLBACK are disabled in its
 * sdkconfig, so the real functions are absent from the static library).
 *
 * mmipal calls these once during init to register a single callback for
 * link-up / IP-configured events. With these stubs, the callback never fires —
 * mmwlan_register_link_state_cb (driven by the radio firmware) remains the
 * authoritative source of link state, so this only costs us LWIP-level
 * notifications (e.g. "DHCP got an address"). HaLowInterface polls
 * mmipal_get_ip_config when it needs to know.
 */
#include "lwip/netif.h"

void __attribute__((weak)) netif_set_link_callback(struct netif *netif, netif_status_callback_fn link_callback)
{
    (void)netif;
    (void)link_callback;
}

void __attribute__((weak)) netif_set_status_callback(struct netif *netif, netif_status_callback_fn status_callback)
{
    (void)netif;
    (void)status_callback;
}
#endif
