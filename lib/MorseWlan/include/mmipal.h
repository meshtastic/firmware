/*
 * Copyright 2023 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @defgroup MMIPAL Morse Micro IP Stack Abstraction Layer (MMIPAL) API
 *
 * This API provides a layer of abstraction from the underlying IP stack for common operations
 * such as configuring the link and getting link status.
 *
 * @{
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/** Maximum length of an IP address string, including null-terminator. */
#ifndef MMIPAL_IPADDR_STR_MAXLEN
#define MMIPAL_IPADDR_STR_MAXLEN (48)
#endif

/** Maximum number of IPv6 addresses supported. */
#ifndef MMIPAL_MAX_IPV6_ADDRESSES
#define MMIPAL_MAX_IPV6_ADDRESSES (3)
#endif

/** Enumeration of status codes returned by MMIPAL functions. */
enum mmipal_status {
    /** Completed successfully. */
    MMIPAL_SUCCESS,
    /** One or more arguments were invalid. */
    MMIPAL_INVALID_ARGUMENT,
    /** The operation could not complete because the link is not up. */
    MMIPAL_NO_LINK,
    /** Failed due to memory allocation failure. */
    MMIPAL_NO_MEM,
    /** This functionality is not supported (e.g., due to build configuration). */
    MMIPAL_NOT_SUPPORTED,
};

/** Enumeration of link states. */
enum mmipal_link_state {
    /** Link is down. */
    MMIPAL_LINK_DOWN,
    /** Link is up. */
    MMIPAL_LINK_UP,
};

/** Enumeration of IP address allocation modes. */
enum mmipal_addr_mode {
    /** Disabled. */
    MMIPAL_DISABLED,
    /** Static IP address. */
    MMIPAL_STATIC,
    /** IP address allocated via DHCP. @c LWIP_DHCP must be set to 1 if using LWIP. */
    MMIPAL_DHCP,
    /** IP address allocated via AutoIP. @c LWIP_DHCP must be set to 1 if using LWIP. */
    MMIPAL_AUTOIP,
    /** DHCP offloaded to chip. */
    MMIPAL_DHCP_OFFLOAD,
};

/** IP address string type. */
typedef char mmipal_ip_addr_t[MMIPAL_IPADDR_STR_MAXLEN];

/**
 * IPv4 configuration structure.
 *
 * This should be initialized using @c MMIPAL_IP_CONFIG_DEFAULT.
 * For example:
 *
 * @code{.c}
 * struct mmipal_ip_config config = MMIPAL_IP_CONFIG_DEAFULT;
 * @endcode
 */
struct mmipal_ip_config {
    /** IP address allocation mode. */
    enum mmipal_addr_mode mode;
    /** local IP address */
    mmipal_ip_addr_t ip_addr;
    /** Netmask address */
    mmipal_ip_addr_t netmask;
    /** Gateway address */
    mmipal_ip_addr_t gateway_addr;
};

/** Initializer for @ref mmipal_ip_config. */
#define MMIPAL_IP_CONFIG_DEFAULT                                                                                                 \
    {                                                                                                                            \
        MMIPAL_DHCP, "", "", "",                                                                                                 \
    }

/** Enumeration of IPv6 address allocation modes. */
enum mmipal_ip6_addr_mode {
    /** Disabled. */
    MMIPAL_IP6_DISABLED,
    /** Static IPv6 addresses. */
    MMIPAL_IP6_STATIC,
    /** IPv6 address allocated via autoconfiguration.
     *  @c LWIP_IPV6_AUTOCONFIG must be set to 1 if using LWIP. */
    MMIPAL_IP6_AUTOCONFIG,
    /** IPv6 address allocated via stateless DHCPv6.
     * @c LWIP_IPV6_DHCP6_STATELESS must be set to 1 if using LWIP. */
    MMIPAL_IP6_DHCP6_STATELESS,
};

/**
 * IPv6 configuration structure.
 *
 * This should be initialized using @c MMIPAL_IP6_CONFIG_DEFAULT.
 * For example:
 *
 * @code{.c}
 * struct mmipal_ip6_config config = MMIPAL_IP6_CONFIG_DEFAULT;
 * @endcode
 */
struct mmipal_ip6_config {
    /** IPv6 addresses allocation mode. */
    enum mmipal_ip6_addr_mode ip6_mode;
    /** Array of IPv6 addresses. */
    mmipal_ip_addr_t ip6_addr[MMIPAL_MAX_IPV6_ADDRESSES];
};

/** Initializer for @ref mmipal_ip6_config. */
#define MMIPAL_IP6_CONFIG_DEFAULT                                                                                                \
    {                                                                                                                            \
        MMIPAL_IP6_AUTOCONFIG                                                                                                    \
    }

/**
 * Initialize arguments structure.
 *
 * This should be initialized using @c MMIPAL_INIT_ARGS_DEFAULT.
 * For example:
 *
 * @code{.c}
 * struct mmipal_init_args args = MMIPAL_INIT_ARGS_DEFAULT;
 * @endcode
 */
struct mmipal_init_args {
    /** IP address allocation mode to use. */
    enum mmipal_addr_mode mode;
    /** IP address to use (if @c mode is @c MMIPAL_STATIC). */
    mmipal_ip_addr_t ip_addr;
    /** Netmask to use (if @c mode is @c MMIPAL_STATIC). */
    mmipal_ip_addr_t netmask;
    /** Gateway IP address to use (if @c mode is @c MMIPAL_STATIC). */
    mmipal_ip_addr_t gateway_addr;

    /** IPv6 address allocation mode to use. */
    enum mmipal_ip6_addr_mode ip6_mode;
    /** IPv6 address to use (if @c ip6_mode is @c MMIPAL_IP6_STATIC). */
    mmipal_ip_addr_t ip6_addr;
    /** Flag requesting ARP response offload feature */
    bool offload_arp_response;
    /** ARP refresh offload interval in seconds */
    uint32_t offload_arp_refresh_s;
};

/**
 * Default values for @ref mmipal_init_args. This should be used when initializing the
 * @ref mmipal_init_args structure.
 */
#define MMIPAL_INIT_ARGS_DEFAULT                                                                                                 \
    {                                                                                                                            \
        MMIPAL_DHCP, {0}, {0}, {0}, MMIPAL_IP6_DISABLED, {0}, false, 0                                                           \
    }

/**
 * Initialize the IP stack and enable the MMWLAN interface.
 *
 * This will implicitly initialize and boot MMWLAN, and will block until this has completed.
 *
 * @note This function will boot the Morse Micro transceiver using @ref mmwlan_boot() in order
 *       to read the MAC address. It is the responsibility of the caller to shut down the
 *       transceiver using @ref mmwlan_shutdown() as required.
 *
 * @warning @ref mmwlan_init() must be called before invoking this function.
 *
 * @param  args Initialization arguments.
 *
 * @return      @c MMIPAL_SUCCESS on success. otherwise a vendor specific error code.
 */
enum mmipal_status mmipal_init(const struct mmipal_init_args *args);

/**
 * Structure representing the current status of the link.
 */
struct mmipal_link_status {
    /** State of the link (up/down). */
    enum mmipal_link_state link_state;
    /** Current IP address. */
    mmipal_ip_addr_t ip_addr;
    /** Current netmask. */
    mmipal_ip_addr_t netmask;
    /** Current gateway IP address. */
    mmipal_ip_addr_t gateway;
};

/**
 * Prototype for callback function invoked on link status changes.
 *
 * @param link_status The current link status.
 */
typedef void (*mmipal_link_status_cb_fn_t)(const struct mmipal_link_status *link_status);

/**
 * Sets the callback function to be invoked on link status changes.
 *
 * This will be used when DHCP is enabled.
 *
 * @note This is for IPv4 only. To get IPv6 status use @c mmipal_get_ip6_config.
 * @note If an opaque argument is required then use @ref mmipal_set_ext_link_status_callback()
 *       instead.
 *
 * @param fn Function pointer to the callback function.
 */
void mmipal_set_link_status_callback(mmipal_link_status_cb_fn_t fn);

/**
 * Prototype for callback function invoked on link status changes.
 *
 * This is similar to @ref mmipal_link_status_cb_fn_t but with the addition of the @p arg
 * parameter.
 *
 * @param link_status The current link status.
 * @param arg         Opaque argument that was provided when the callback was registered.
 */
typedef void (*mmipal_ext_link_status_cb_fn_t)(const struct mmipal_link_status *link_status, void *arg);

/**
 * Sets the extended link status callback function to be invoked on link status changes.
 * This is similar to @ref mmipal_set_link_status_callback() with the exception that
 * an opaque argument may also be specified.
 *
 * This will be used when DHCP is enabled.
 *
 * @note This is for IPv4 only. To get IPv6 status use @c mmipal_get_ip6_config.
 *
 * @param fn  Function pointer to the callback function.
 * @param arg Opaque argument to be passed to the callback.
 */
void mmipal_set_ext_link_status_callback(mmipal_ext_link_status_cb_fn_t fn, void *arg);

/**
 * Get the total number of transmitted and received packets on the MMWLAN interface
 *
 * @note If using LWIP, this function requires LWIP_STATS to be defined in your application,
 *       otherwise packet counters will always return as 0.
 *
 * @param tx_packets Pointer to location to store total tx packets
 * @param rx_packets Pointer to location to store total rx packets
 */
void mmipal_get_link_packet_counts(uint32_t *tx_packets, uint32_t *rx_packets);

/**
 * Set the QoS Traffic ID to use when transmitting.
 *
 * @param tid The QoS TID to use (0 - @ref MMWLAN_MAX_QOS_TID).
 */
void mmipal_set_tx_qos_tid(uint8_t tid);

/**
 * Gets the local address for the MMWLAN interface that is appropriate for a given
 * destination address.
 *
 * The following table shows how the returned @c local_addr is selected:
 *
 * | @p dest_addr | @c local_addr returned    |
 * |--------------|---------------------------|
 * | type is IPv4 | IPv4 address              |
 * | type is IPv6 | An IPv6 source address selected from interface's IPv6 addresses or ERR_CONN |
 *
 * (X = don't care)
 *
 * If the given parameters would result in a @p local_addr type of IPv4 and IPv4 is not enabled,
 * or IPv6 and IPv6 is not enabled, then @c MMIPAL_INVALID_ARGUMENT will be returned.
 *
 * @param[out] local_addr Output local address for the MMWLAN interface, as noted above.
 * @param[in]  dest_addr  Destination address.
 *
 * @return                @c MMIPAL_SUCESS if @p local_addr successfully set. otherwise an
 *                        appropriate error code.
 */
enum mmipal_status mmipal_get_local_addr(mmipal_ip_addr_t local_addr, const mmipal_ip_addr_t dest_addr);

/**
 * Get the IP configurations.
 *
 * This can be used to get the local IP configurations.
 *
 * @param config Pointer to the IP configurations.
 *
 * @returns @c MMIPAL_SUCCESS on success, @c MMIPAL_NOT_SUPPORTED if IPv4 is not supported.
 */
enum mmipal_status mmipal_get_ip_config(struct mmipal_ip_config *config);

/**
 * Set the IP configurations.
 *
 * This can be used to set the local IP configurations.
 *
 * @param config Pointer to the IP configurations.
 *
 * @returns @c MMIPAL_SUCCESS on success, @c MMIPAL_NOT_SUPPORTED if IPv4 is not supported.
 */
enum mmipal_status mmipal_set_ip_config(const struct mmipal_ip_config *config);

/**
 * Gets the current IPv4 broadcast address.
 *
 * @param[out] broadcast_addr Buffer to receive the broadcast address as a string.
 *
 * @returns @c MMIPAL_SUCCESS on success, @c MMIPAL_NOT_SUPPORTED if IPv4 is not supported.
 */
enum mmipal_status mmipal_get_ip_broadcast_addr(mmipal_ip_addr_t broadcast_addr);

/**
 * Get the IP configurations.
 *
 * This can be used to get the local IP configurations.
 *
 * @param config Pointer to the IP configurations.
 *
 * @returns @c MMIPAL_SUCCESS on success, @c MMIPAL_NOT_SUPPORTED if IPv6 is not supported..
 */
enum mmipal_status mmipal_get_ip6_config(struct mmipal_ip6_config *config);

/**
 * Set the IPv6 configurations.
 *
 * This can be used to set the local IPv6 configurations.
 *
 * @param config Pointer to the IPv6 configurations.
 *
 * @returns @c MMIPAL_SUCCESS on success, @c MMIPAL_NOT_SUPPORTED if IPv6 is not supported.
 */
enum mmipal_status mmipal_set_ip6_config(const struct mmipal_ip6_config *config);

/**
 * Get current IPv4 link state.
 *
 * @returns the current IPv4 link state (up or down).
 */
enum mmipal_link_state mmipal_get_link_state(void);

/**
 * Set the DNS server at the given index.
 *
 * @warning Depending on IP stack implementation, this setting may be overridden by DHCP.
 *
 * @param[in]  index Index of the DNS server to set.
 * @param[out] addr  Address of the DNS server to set.
 *
 * @returns @c MMIPAL_SUCCESS on success, @c MMIPAL_INVALID_ARGUMENT if an invalid index or IP
 *          address was given.
 */
enum mmipal_status mmipal_set_dns_server(uint8_t index, const mmipal_ip_addr_t addr);

/**
 * Get the DNS server at the given index.
 *
 * @param[in]  index Index of the DNS server to set.
 * @param[out] addr  IP address buffer to receive the IP address of the DNS server at the given
 *                   index. Will be set to empty string if no server at the given index.
 *
 * @returns @c MMIPAL_SUCCESS on success.
 */
enum mmipal_status mmipal_get_dns_server(uint8_t index, mmipal_ip_addr_t addr);

#ifdef __cplusplus
}
#endif

/** @} */
