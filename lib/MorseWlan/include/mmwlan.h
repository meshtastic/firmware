/*
 * Copyright 2021-2024 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @defgroup MMWLAN Morse Micro Wireless LAN (mmwlan) API
 *
 * Wireless LAN control and datapath.
 *
 * @warning Aside from specific exceptions, the functions in this API must not be called
 *          concurrently (e.g., from different thread contexts). The exception to this
 *          is the TX API (@ref mmwlan_tx() and @ref mmwlan_tx_tid()).
 *
 * @section MMWLAN_THREADS Thread priorities
 *
 * The following table documents the threads created by the Morse WLAN driver and Upper MAC
 * included in morselib.
 *
 * | Thread            | Thread Name             | Priority                  |
 * | ----------------- | ----------------------- | ------------------------- |
 * | SPI driver        | @c spi_irq              | @ref MMOSAL_TASK_PRI_HIGH |
 * | Morse driver      | @c drv                  | @ref MMOSAL_TASK_PRI_HIGH |
 * | WLAN event loop   | @c evtloop              | @ref MMOSAL_TASK_PRI_HIGH |
 * | Morse health check| @c health               | @ref MMOSAL_TASK_PRI_LOW  |
 *
 * Note that to get the best performance, the WLAN driver/UMAC threads run at a high priority
 * while it is expected that application threads run at a lower priority.
 *
 * @{
 */

#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "mmpkt.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Enumeration of status return codes. */
enum mmwlan_status {
    /** The operation was successful. */
    MMWLAN_SUCCESS,
    /** The operation failed with an unspecified error. */
    MMWLAN_ERROR,
    /** The operation failed due to an invalid argument. */
    MMWLAN_INVALID_ARGUMENT,
    /** Functionality is temporarily unavailable. */
    MMWLAN_UNAVAILABLE,
    /** Unable to proceed because channel list has not been set.
     *  @see mmwlan_set_channel_list(). */
    MMWLAN_CHANNEL_LIST_NOT_SET,
    /** Failed due to memory allocation failure. */
    MMWLAN_NO_MEM,
    /** Failed due to timeout */
    MMWLAN_TIMED_OUT,
    /** Used to indicate that a call to @ref mmwlan_sta_disable() did not shutdown
     *  the transceiver. */
    MMWLAN_SHUTDOWN_BLOCKED,
    /** Attempted to tune to a channel that was not in the regulatory database or not supported. */
    MMWLAN_CHANNEL_INVALID,
    /** The request could not be completed because the given resource was not found. */
    MMWLAN_NOT_FOUND,
    /** Indicates that the operation failed because the UMAC was not running (e.g., the
     *  device was not booted).  */
    MMWLAN_NOT_RUNNING,
};

/** Maximum allowable length of an SSID. */
#define MMWLAN_SSID_MAXLEN (32)

/** Maximum allowable length of a passphrase when connecting to an AP. */
#define MMWLAN_PASSPHRASE_MAXLEN (100)

/** Maximum allowable Restricted Access Window (RAW) priority for STA. */
#define MMWLAN_RAW_MAX_PRIORITY (7)

/** Length of a WLAN MAC address. */
#define MMWLAN_MAC_ADDR_LEN (6)

/** Maximum allowable number of EC Groups. */
#define MMWLAN_MAX_EC_GROUPS (4)

/** Size of an 802.11 OUI element in octets. */
#define MMWLAN_OUI_SIZE (3)

/** Default Background scan short interval in seconds.
 *  Setting to 0 will disable background scanning. */
#define DEFAULT_BGSCAN_SHORT_INTERVAL_S (0)

/** Default Background scan signal threshold in dBm. */
#define DEFAULT_BGSCAN_THRESHOLD_DBM (0)

/** Default Background scan long interval in seconds.
 *  Setting to 0 will disable background scanning. */
#define DEFAULT_BGSCAN_LONG_INTERVAL_S (0)

/** Default Target Wake Time (TWT) interval in micro seconds. */
#define DEFAULT_TWT_WAKE_INTERVAL_US (300000000)

/** Default min Target Wake Time (TWT) duration in micro seconds. */
#define DEFAULT_TWT_MIN_WAKE_DURATION_US (65280)

/**
 * The maximum length of a user-specified payload (bytes) for Standby status
 * frames.
 */
#define MMWLAN_STANDBY_STATUS_FRAME_USER_PAYLOAD_MAXLEN (64)

/** The maximum allowed length of a user filter to apply to wake frames */
#define MMWLAN_STANDBY_WAKE_FRAME_USER_FILTER_MAXLEN (64)

/** Enumeration of supported security types. */
enum mmwlan_security_type {
    /** Open (no security) */
    MMWLAN_OPEN,
    /** Opportunistic Wireless Encryption (OWE) */
    MMWLAN_OWE,
    /** Simultaneous Authentication of Equals (SAE) */
    MMWLAN_SAE,
};

/** Enumeration of supported 802.11 power save modes. */
enum mmwlan_ps_mode {
    /** Power save disabled */
    MMWLAN_PS_DISABLED,
    /** Power save enabled */
    MMWLAN_PS_ENABLED
};

/** Enumeration of Protected Management Frame (PMF) modes (802.11w). */
enum mmwlan_pmf_mode {
    /** Protected management frames must be used */
    MMWLAN_PMF_REQUIRED,
    /** No protected management frames */
    MMWLAN_PMF_DISABLED
};

/** Enumeration of Centralized Authentication Control (CAC) modes. */
enum mmwlan_cac_mode {
    /** CAC disabled */
    MMWLAN_CAC_DISABLED,
    /** CAC enabled */
    MMWLAN_CAC_ENABLED
};

/**
 * Enumeration of Target Wake Time (TWT) modes.
 *
 * @note TWT is only supported as a requester.
 */
enum mmwlan_twt_mode {
    /** TWT disabled */
    MMWLAN_TWT_DISABLED,
    /** TWT enabled as a requester */
    MMWLAN_TWT_REQUESTER,
    /** TWT enabled as a responder */
    MMWLAN_TWT_RESPONDER
};

/** Enumeration of Target Wake Time (TWT) setup commands. */
enum mmwlan_twt_setup_command {
    /** TWT setup request command */
    MMWLAN_TWT_SETUP_REQUEST,
    /** TWT setup suggest command */
    MMWLAN_TWT_SETUP_SUGGEST,
    /** TWT setup demand command */
    MMWLAN_TWT_SETUP_DEMAND
};

/**
 * @defgroup MMWLAN_REGDB    WLAN Regulatory Database API
 *
 * @{
 */

/**
 * If either the global or s1g operating class is set to this, the operating class will
 * not be checked when associating to an AP.
 */
#define MMWLAN_SKIP_OP_CLASS_CHECK -1

/** Regulatory domain information about an S1G channel. */
struct mmwlan_s1g_channel {
    /** Center frequency of the channel (in Hz). */
    uint32_t centre_freq_hz;
    /** STA Duty Cycle (in 100th of %). */
    uint16_t duty_cycle_sta;
    /** Boolean indicating whether to omit control response frames from duty cycle. */
    bool duty_cycle_omit_ctrl_resp;
    /** Global operating class. If @ref MMWLAN_SKIP_OP_CLASS_CHECK, check is skipped */
    int16_t global_operating_class;
    /** S1G operating class. If @ref MMWLAN_SKIP_OP_CLASS_CHECK, check is skipped */
    int16_t s1g_operating_class;
    /** S1G channel number. */
    uint8_t s1g_chan_num;
    /** Channel operating bandwidth (in MHz). */
    uint8_t bw_mhz;
    /** Maximum transmit power (EIRP in dBm). */
    int8_t max_tx_eirp_dbm;
    /** The length of time to close the tx window between packets (in microseconds). */
    uint32_t pkt_spacing_us;
    /** The minimum packet airtime duration to trigger spacing (in microseconds). */
    uint32_t airtime_min_us;
    /** The maximum allowable packet airtime duration (in microseconds). */
    uint32_t airtime_max_us;
};

/** A list of S1G channels supported by a given regulatory domain. */
struct mmwlan_s1g_channel_list {
    /** Two character country code (null-terminated) used to identify the regulatory domain. */
    uint8_t country_code[3];
    /** The number of channels in the list. */
    unsigned num_channels;
    /** The channel data. */
    const struct mmwlan_s1g_channel *channels;
};

/**
 * Regulatory database data structure. This is a list of @c mmwlan_s1g_channel_list structs, where
 * each channel list corresponds to a regulatory domain.
 */
struct mmwlan_regulatory_db {
    /** Number of regulatory domains in the database. */
    unsigned num_domains;

    /** The regulatory domain data */
    const struct mmwlan_s1g_channel_list **domains;
};

/**
 * Look up the given country code in the regulatory database and return the matching channel
 * list if found.
 *
 * @param db            The regulatory database.
 * @param country_code  Country code to look up.
 *
 * @returns the matching channel list if found, else NULL.
 */
static inline const struct mmwlan_s1g_channel_list *mmwlan_lookup_regulatory_domain(const struct mmwlan_regulatory_db *db,
                                                                                    const char *country_code)
{
    unsigned ii;

    if (db == NULL) {
        return NULL;
    }

    for (ii = 0; ii < db->num_domains; ii++) {
        const struct mmwlan_s1g_channel_list *channel_list = db->domains[ii];
        if (channel_list->country_code[0] == country_code[0] && channel_list->country_code[1] == country_code[1]) {
            return channel_list;
        }
    }
    return NULL;
}

/**
 * Set the list of channels that are supported by the regulatory domain in which the device
 * resides.
 *
 * @note Must be invoked after WLAN initialization (see @ref mmwlan_init()) but only when inactive
 *       (i.e., STA not enabled).
 *
 * @warning This function takes a reference to the given channel list. It expects the channel
 *          list to remain valid in memory as long as WLAN is in use, or until a new channel
 *          list is configured.
 *
 * @param channel_list  The channel list to set. The list must remain valid in memory.
 *
 * @return @ref MMWLAN_SUCCESS if the channel list was valid and updated successfully,
 *         @ref MMWLAN_UNAVAILABLE if the WLAN subsystem was currently active.
 */
enum mmwlan_status mmwlan_set_channel_list(const struct mmwlan_s1g_channel_list *channel_list);

/** @} */

/**
 * @defgroup MMWLAN_CTRL    WLAN Control API
 *
 * @{
 */

/** Maximum length of the Morselib version string. */
#define MMWLAN_MORSELIB_VERSION_MAXLEN (32)
/** Maximum length of the firmware version string. */
#define MMWLAN_FW_VERSION_MAXLEN (32)

/** Structure for retrieving version information from the mmwlan subsystem. */
struct mmwlan_version {
    /** Morselib version string. Null terminated. */
    char morselib_version[MMWLAN_MORSELIB_VERSION_MAXLEN];
    /** Morse transceiver firmware version string. Null terminated. */
    char morse_fw_version[MMWLAN_FW_VERSION_MAXLEN];
    /** Morse transceiver chip ID. */
    uint32_t morse_chip_id;
};

/**
 * Retrieve version information from morselib and the connected Morse transceiver.
 *
 * @param version   The data structure to fill out with version information.
 *
 * @note If the Morse transceiver has not previously been powered on then the @c fw_version
 *       field of @p version will be set to a zero length string.
 *
 * @returns @c MMWLAN_SUCCESS on success else an error code.
 */
enum mmwlan_status mmwlan_get_version(struct mmwlan_version *version);

/** Maximum length of a BCF board description string (excluding null terminator). */
#define MMWLAN_BCF_BOARD_DESC_MAXLEN (31)
/** Maximum length of a BCF build version string (excluding null terminator). */
#define MMWLAN_BCF_BUILD_VERSION_MAXLEN (31)

/** Board configuration file (BCF) metadata. */
struct mmwlan_bcf_metadata {
    /** BCF semantic version. */
    struct {
        /** Major version field. */
        uint16_t major;
        /** Minor version field. */
        uint8_t minor;
        /** Patch version field. */
        uint8_t patch;
    } version;

    /**
     * Board description string. This is a free form text field included in the BCF.
     *
     * This string will be null-terminated and if it exceeds @ref MMWLAN_BCF_BOARD_DESC_MAXLEN
     * characters in length it will be truncated.
     */
    char board_desc[MMWLAN_BCF_BOARD_DESC_MAXLEN + 1];

    /**
     * Build version string.
     *
     * This string will be null-terminated and if it exceeds @ref MMWLAN_BCF_BOARD_DESC_MAXLEN
     * characters in length it will be truncated.
     */
    char build_version[MMWLAN_BCF_BUILD_VERSION_MAXLEN + 1];
};

/**
 * Read the metadata from the board configuration file (BCF)r.
 *
 * @param metadata  Pointer to a metadata data structure to be filled out on success.
 *
 * @returns @c MMWLAN_SUCCESS on success else an error code.
 */
enum mmwlan_status mmwlan_get_bcf_metadata(struct mmwlan_bcf_metadata *metadata);

/**
 * Override the maximum TX power. If no override is specified then the maximum TX power used
 * will be the maximum TX power allowed for the channel in the current regulatory domain.
 *
 * @note Must be invoked after WLAN initialization (see @ref mmwlan_init()). Only takes
 *       effect when switching channel (e.g., during scan or AP connection procedure).
 *
 * @note This will not increase the TX power over the maximum allowed for the current channel
 *       in the configured regulatory domain. Therefore, this override in effect will only
 *       reduce the maximum TX power and cannot increase it.
 *
 * @param tx_power_dbm  The maximum TX power override to set (in dBm). Set to zero to disable
 *                      the override.
 *
 * @return @ref MMWLAN_SUCCESS if the country code was valid and updated successfully,
 *         @ref MMWLAN_UNAVAILABLE if the WLAN subsystem was currently active.
 */
enum mmwlan_status mmwlan_override_max_tx_power(uint16_t tx_power_dbm);

/**
 * Set the RTS threshold.
 *
 * When packets larger than the RTS threshold are transmitted they are protected by an RTS/CTS
 * exchange.
 *
 * @param rts_threshold The RTS threshold (in octets) to set, or 0 to disable.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_set_rts_threshold(unsigned rts_threshold);

/**
 * Sets whether or not Short Guard Interval (SGI) support is enabled. Defaults to enabled
 * if not set otherwise.
 *
 * @note This will not force use of SGI, only enable support for it. The rate control algorithm
 *       will make the decision as to which guard interval to use unless explicitly overridden
 *       by @ref mmwlan_ate_override_rate_control().
 *
 * @note This must only be invoked when MMWLAN is inactive (i.e., STA mode not enabled).
 *
 * @param sgi_enabled   Boolean value indicating whether SGI support should be enabled.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_set_sgi_enabled(bool sgi_enabled);

/**
 * Sets whether or not sub-band support is enabled for transmit. Defaults to enabled
 * if not set otherwise.
 *
 * @note This will not force use of sub-bands, only enable support for it. The rate control
 *       algorithm will make the decision as to which bandwidth to use unless explicitly overridden
 *       by @ref mmwlan_ate_override_rate_control().
 *
 * @note This must only be invoked when MMWLAN is inactive (i.e., STA mode not enabled).
 *
 * @param subbands_enabled   Boolean value indicating whether sub-band support should be enabled.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_set_subbands_enabled(bool subbands_enabled);

/**
 * Sets whether or not the 802.11 power save is enabled. Defaults to @ref MMWLAN_PS_ENABLED
 *
 * @param mode   enum indicating which 802.11 power save mode to use.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_set_power_save_mode(enum mmwlan_ps_mode mode);

/**
 * Sets whether or not Aggregated MAC Protocol Data Unit (A-MPDU) support is enabled.
 * This defaults to enabled, if not set otherwise.
 *
 * @note This must only be invoked when MMWLAN is inactive (i.e., STA mode not enabled).
 *
 * @param ampdu_enabled   Boolean value indicating whether AMPDU support should be enabled.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_set_ampdu_enabled(bool ampdu_enabled);

/**
 * Minimum value of fragmentation threshold that can be set with
 * @ref mmwlan_set_fragment_threshold().
 */
#define MMWLAN_MINIMUM_FRAGMENT_THRESHOLD (256)

/**
 * Set the Fragmentation threshold.
 *
 * Maximum length of the frame, beyond which packets must be fragmented into two or more frames.
 *
 * @note Even if the fragmentation threshold is set to 0 (disabled), fragmentation may still occur
 *       if a given packet is too large to be transmitted at the selected rate.
 *
 * @warning Setting a fragmentation threshold may have unintended side effects due to restrictions
 *          at lower bandwidths and MCS rates. In normal operation the fragmentation threshold
 *          should be disabled, in which case packets will be fragmented automatically as necessary
 *          based on the selected rate.
 *
 * @param fragment_threshold The fragmentation threshold (in octets) to set,
 *                           or zero to disable the fragmentation threshold. Minimum value
 *                           (if not zero) is given by @ref MMWLAN_MINIMUM_FRAGMENT_THRESHOLD.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_set_fragment_threshold(unsigned fragment_threshold);

/**
 * Scan configuration data structure.
 *
 * Use @ref MMWLAN_SCAN_CONFIG_INIT for initialization. For example:
 *
 * @code{.c}
 * struct mmwlan_scan_config scan_config = MMWLAN_SCAN_CONFIG_INIT;
 * @endcode
 *
 * @see mmwlan_set_scan_config()
 */
struct mmwlan_scan_config {
    /**
     * Set the per-channel dwell time to use for scans that are requested internally within the
     * mmwlan driver (e.g., when connecting or background scanning).
     *
     * @note This does not affect scans requested with the @ref mmwlan_scan_request().
     */
    uint32_t dwell_time_ms;
};

/** Initializer for @ref mmwlan_scan_config. */
#define MMWLAN_SCAN_CONFIG_INIT                                                                                                  \
    {                                                                                                                            \
        MMWLAN_SCAN_DEFAULT_DWELL_TIME_MS                                                                                        \
    }

/**
 * Update the scan configuration with the given settings.
 *
 * @param config    The new configuration to set.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_set_scan_config(const struct mmwlan_scan_config *config);

/** Structure for storing Target Wake Time (TWT) configuration arguments. */
struct mmwlan_twt_config_args {
    /** Target Wake Time (TWT) modes, @ref mmwlan_twt_mode. */
    enum mmwlan_twt_mode twt_mode;
    /**
     * TWT service period interval in micro seconds.
     * This parameter will be ignored if @c twt_wake_interval_mantissa or
     * @c twt_wake_interval_exponent is non-zero.
     */
    uint64_t twt_wake_interval_us;
    /**
     * TWT Wake interval mantissa
     * If non-zero, this parameter will be used to calculate @c twt_wake_interval_us.
     */
    uint16_t twt_wake_interval_mantissa;
    /**
     * TWT Wake interval exponent
     * If non-zero, this parameter will be used to calculate @c twt_wake_interval_us.
     */
    uint8_t twt_wake_interval_exponent;
    /** Minimum TWT wake duration in micro seconds. */
    uint32_t twt_min_wake_duration_us;
    /** TWT setup command, @ref mmwlan_twt_setup_command. */
    enum mmwlan_twt_setup_command twt_setup_command;
};

/**
 * Initializer for @ref mmwlan_twt_config_args.
 *
 * For example:
 *
 * @code{c}
 * struct mmwlan_twt_config_args twt_config_args = MMWLAN_TWT_CONFIG_ARGS_INIT;
 * @endcode
 */
#define MMWLAN_TWT_CONFIG_ARGS_INIT                                                                                              \
    {                                                                                                                            \
        MMWLAN_TWT_DISABLED, DEFAULT_TWT_WAKE_INTERVAL_US, 0, 0, DEFAULT_TWT_MIN_WAKE_DURATION_US, MMWLAN_TWT_SETUP_REQUEST      \
    }

/**
 * Add configurations for Target Wake Time (TWT).
 *
 * @note This is used to add TWT configuration for a new TWT agreement.
 *       This function should be invoked before @ref mmwlan_sta_enable.
 *
 * @param twt_config_args TWT configuration arguments @ref mmwlan_twt_config_args.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_twt_add_configuration(const struct mmwlan_twt_config_args *twt_config_args);

/**
 * Arguments data structure for @ref mmwlan_boot().
 *
 * This structure should be initialized using @ref MMWLAN_BOOT_ARGS_INIT for sensible
 * default values, particularly for forward compatibility with new releases that may add
 * new fields to the struct. For example:
 *
 * @note This struct currently does not include any sensible arguments yet, and is provided
 *       for forwards compatibility with potential future API extensions.
 *
 * @code{.c}
 *     enum mmwlan_status status;
 *     struct mmwlan_boot_args boot_args = MMWLAN_BOOT_ARGS_INIT;
 *     // HERE: initialize arguments
 *     status = mmwlan_boot(&boot_args);
 * @endcode
 */
struct mmwlan_boot_args {
    /** Note this field should not be used and will be removed in future. */
    uint8_t reserved;
};

/**
 * Initializer for @ref mmwlan_boot_args.
 *
 * @see mmwlan_boot_args
 */
#define MMWLAN_BOOT_ARGS_INIT                                                                                                    \
    {                                                                                                                            \
        0                                                                                                                        \
    }

/**
 * Boot the Morse Micro transceiver and leave it in an idle state.
 *
 * @note In general, it is not necessary to use this function as @ref mmwlan_sta_enable()
 *       will automatically boot the chip if required. It may be used, for example, to power
 *       on the chip for production test, etc.
 *
 * @warning Channel list must be set before booting the transceiver. @ref mmwlan_set_channel_list().
 *
 * @param args  Boot arguments. May be @c NULL, in which case default values will be used.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_boot(const struct mmwlan_boot_args *args);

/**
 * Perform a clean shutdown of the Morse Micro transceiver, including cleanly disconnecting
 * from a connected AP, if necessary.
 *
 * Has no effect if the transceiver is already shutdown.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_shutdown(void);

/**
 * Enumeration of states in STA mode.
 */
enum mmwlan_sta_state {
    MMWLAN_STA_DISABLED,
    MMWLAN_STA_CONNECTING,
    MMWLAN_STA_CONNECTED,
};

/**
 * Enumeration of S1G non-AP STA types.
 */
enum mmwlan_station_type {
    MMWLAN_STA_TYPE_SENSOR = 0x01,
    MMWLAN_STA_TYPE_NON_SENSOR = 0x02,
};

/** Result of the scan request. */
struct mmwlan_scan_result {
    /** RSSI of the received frame. */
    int16_t rssi;
    /** Pointer to the BSSID field within the Probe Response frame. */
    const uint8_t *bssid;
    /** Pointer to the SSID within the SSID IE of the Probe Response frame. */
    const uint8_t *ssid;
    /** Pointer to the start of the Information Elements within the Probe Response frame. */
    const uint8_t *ies;
    /** Value of the Beacon Interval field. */
    uint16_t beacon_interval;
    /** Value of the Capability Information  field. */
    uint16_t capability_info;
    /** Length of the Information Elements (@c ies). */
    uint16_t ies_len;
    /** Length of the SSID (@c ssid). */
    uint8_t ssid_len;
    /** Center frequency in Hz of the channel where the frame was received. */
    uint32_t channel_freq_hz;
    /** Bandwidth, in MHz, where the frame was received. */
    uint8_t bw_mhz;
    /** Operating bandwidth, in MHz, of the access point. */
    uint8_t op_bw_mhz;
    /** TSF timestamp in the Probe Response frame. */
    uint64_t tsf;
};

/** mmwlan scan rx callback function prototype. */
typedef void (*mmwlan_scan_rx_cb_t)(const struct mmwlan_scan_result *result, void *arg);

/** STA status callback function prototype. */
typedef void (*mmwlan_sta_status_cb_t)(enum mmwlan_sta_state sta_state);

/**
 * Arguments data structure for @ref mmwlan_sta_enable().
 *
 * This structure should be initialized using @ref MMWLAN_STA_ARGS_INIT for sensible
 * default values, particularly for forward compatibility with new releases that may add
 * new fields to the struct. For example:
 *
 * @code{.c}
 *     enum mmwlan_status status;
 *     struct mmwlan_sta_args sta_args = MMWLAN_STA_ARGS_INIT;
 *     // HERE: initialize arguments
 *     status = mmwlan_sta_enable(&sta_args);
 * @endcode
 */
struct mmwlan_sta_args {
    /** SSID of the AP to connect to. */
    uint8_t ssid[MMWLAN_SSID_MAXLEN];
    /** Length of the SSID. */
    uint16_t ssid_len;
    /**
     * BSSID of the AP to connect to.
     * If non-zero, the STA will only connect to an AP that matches this value.
     */
    uint8_t bssid[MMWLAN_MAC_ADDR_LEN];
    /** Type of security to use. If @c MMWLAN_SAE then a @c passphrase must be specified. */
    enum mmwlan_security_type security_type;
    /** Passphrase (only used if @c security_type is @c MMWLAN_SAE, otherwise ignored. */
    char passphrase[MMWLAN_PASSPHRASE_MAXLEN + 1];
    /** Length of @c passphrase. May be zero if @c passphrase is null-terminated. */
    uint16_t passphrase_len;
    /** Protected Management Frame mode to use (802.11w) */
    enum mmwlan_pmf_mode pmf_mode;
    /**
     * Priority used by the AP to assign a STA to a Restricted Access Window (RAW) group.
     * Valid range is 0 - @ref MMWLAN_RAW_MAX_PRIORITY, or -1 to disable RAW.
     */
    int16_t raw_sta_priority;
    /** S1G non-AP STA type. For valid STA types, @ref mmwlan_station_type */
    enum mmwlan_station_type sta_type;
    /**
     * Preference list of enabled elliptic curve groups for SAE and OWE.
     * By default (if this parameter is not set), the mandatory group 19 is preferred.
     */
    int sae_owe_ec_groups[MMWLAN_MAX_EC_GROUPS];
    /** Whether Centralized Authentication Controlled is enabled on the STA. */
    enum mmwlan_cac_mode cac_mode;
    /**
     * Background scan short interval, measured in seconds.
     *
     * When the signal strength falls below @c bgscan_signal_threshold_dbm, this interval
     * will be used between iterations of background scan. After several iterations, or
     * if the signal threshold increases above @c bgscan_signal_threshold_dbm background
     * scan will return to using the long interval.
     *
     * @note Setting this to zero will disable background scanning.
     */
    uint16_t bgscan_short_interval_s;
    /**
     * Background scan signal strength threshold that switches between short and long intervals.
     */
    int bgscan_signal_threshold_dbm;
    /**
     * Background scan long interval, measured in seconds.
     *
     * When the signal strength is above @c bgscan_signal_threshold_dbm, this interval
     * will be used between iterations of background scan. If the signal threshold
     * falls below @c bgscan_signal_threshold_dbm, background scan will use the short
     * interval.
     *
     * @note Setting this to zero will disable background scanning.
     */
    uint16_t bgscan_long_interval_s;
    /** Optional callback for scan results which are received during the connection process. */
    mmwlan_scan_rx_cb_t scan_rx_cb;
    /** Opaque argument to be passed to @ref scan_rx_cb. */
    void *scan_rx_cb_arg;
};

/**
 * Initializer for @ref mmwlan_sta_args.
 *
 * @see mmwlan_sta_args
 */
#define MMWLAN_STA_ARGS_INIT                                                                                                     \
    {                                                                                                                            \
        {0}, 0, {0}, MMWLAN_OPEN, {0}, 0, MMWLAN_PMF_REQUIRED, -1, MMWLAN_STA_TYPE_NON_SENSOR, {0}, MMWLAN_CAC_DISABLED,         \
            DEFAULT_BGSCAN_SHORT_INTERVAL_S, DEFAULT_BGSCAN_THRESHOLD_DBM, DEFAULT_BGSCAN_LONG_INTERVAL_S, NULL, NULL            \
    }

/**
 * Enable station mode.
 *
 * This will power on the transceiver then initiate connection to the given Access Point. If
 * station mode is already enabled when this function is invoked then it will disconnect from
 * (if already connected) and initiate connection to the given AP.
 *
 * @note The STA status callback (@p sta_status_cb) must not block and MMWLAN API functions
 *       may not be invoked from the callback.
 *
 * @warning Channel list must be set before enabling station mode. @ref mmwlan_set_channel_list().
 *
 * @param args              STA arguments (e.g., SSID, etc.). See @ref mmwlan_sta_args.
 * @param sta_status_cb     Optional callback to be invoked on STA state changes. May be @c NULL.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_sta_enable(const struct mmwlan_sta_args *args, mmwlan_sta_status_cb_t sta_status_cb);

/**
 * Disable station mode.
 *
 * This will disconnect from the AP. It will also shut down the transceiver if nothing else
 * is holding it open. Note that if the transceiver was booted by @c mmwlan_boot() then
 * this function will shut down the transceiver.
 *
 * @return @ref MMWLAN_SUCCESS if successful and the transceiver was also shut down,
 *         @ref MMWLAN_SHUTDOWN_BLOCKED if successful and the transceiver was not shut down,
 *         else an appropriate error code.
 */
enum mmwlan_status mmwlan_sta_disable(void);

/**
 * Gets the current WLAN STA state.
 *
 * @return the current WLAN STA state.
 */
enum mmwlan_sta_state mmwlan_get_sta_state(void);

/** Default value for @c mmwlan_scan_args.dwell_time_ms. Note that reducing the dwell time
 *  below this value may impact scan reliability. */
#define MMWLAN_SCAN_DEFAULT_DWELL_TIME_MS (105)

/** Minimum value for @c mmwlan_scan_args.dwell_time_ms. */
#define MMWLAN_SCAN_MIN_DWELL_TIME_MS (15)

/**
 * Enumeration of states in Scan mode.
 */
enum mmwlan_scan_state {
    /** Scan was successful and all channels were scanned. */
    MMWLAN_SCAN_SUCCESSFUL,
    /** Scan was incomplete. One or more channels may have been scanned and therefore an
     *  incomplete set of scan results may still have been received. */
    MMWLAN_SCAN_TERMINATED,
    /** Scanning in progress. */
    MMWLAN_SCAN_RUNNING,
};

/** mmwlan scan complete callback function prototype. */
typedef void (*mmwlan_scan_complete_cb_t)(enum mmwlan_scan_state scan_state, void *arg);

/**
 * Structure to hold scan arguments. This structure should be initialized using
 * @ref MMWLAN_SCAN_ARGS_INIT for forward compatibility.
 */
struct mmwlan_scan_args {
    /**
     * Time to dwell on a channel waiting for probe responses/beacons.
     *
     * @note This includes the time it takes to actually tune to the channel.
     */
    uint32_t dwell_time_ms;
    /**
     * Extra Information Elements to include in Probe Request frames.
     * May be @c NULL if @c extra_ies_len is zero.
     */
    uint8_t *extra_ies;
    /** Length of @c extra_ies. */
    size_t extra_ies_len;
    /**
     * SSID used for scan.
     * May be @c NULL, with @c ssid_len set to zero for an undirected scan.
     */
    uint8_t ssid[MMWLAN_SSID_MAXLEN];
    /** Length of the SSID. */
    uint16_t ssid_len;
};

/**
 * Initializer for @ref mmwlan_scan_args.
 *
 * For example:
 *
 * @code{c}
 * struct mmwlan_scan_args scan_args = MMWLAN_SCAN_ARGS_INIT;
 * @endcode
 */
#define MMWLAN_SCAN_ARGS_INIT                                                                                                    \
    {                                                                                                                            \
        MMWLAN_SCAN_DEFAULT_DWELL_TIME_MS, NULL, 0, {0}, 0                                                                       \
    }

/**
 * Structure to hold arguments specific to a given instance of a scan.
 */
struct mmwlan_scan_req {
    /** Scan response receive callback. Must not be @c NULL. */
    mmwlan_scan_rx_cb_t scan_rx_cb;
    /** Scan complete callback. Must not be @c NULL. */
    mmwlan_scan_complete_cb_t scan_complete_cb;
    /** Opaque argument to be passed to the callbacks. */
    void *scan_cb_arg;
    /** Scan arguments to be used @ref mmwlan_scan_args. */
    struct mmwlan_scan_args args;
};

/**
 * Initializer for @ref mmwlan_scan_req.
 *
 * For example:
 *
 * @code{c}
 * struct mmwlan_scan_req scan_req = MMWLAN_SCAN_REQ_INIT;
 * @endcode
 */
#define MMWLAN_SCAN_REQ_INIT                                                                                                     \
    {                                                                                                                            \
        NULL, NULL, NULL, MMWLAN_SCAN_ARGS_INIT                                                                                  \
    }

/**
 * Request a scan.
 *
 * If the transceiver is not already powered on, it will be powered on before the scan is
 * initiated. The power on procedure will block this function. The transceiver will remain
 * powered on after scan completion and must be shutdown by invoking @ref mmwlan_shutdown().
 *
 * @note Just because a scan is requested does not mean it will happen immediately.
 *       It may take some time for the request to be serviced.
 *
 * @note The scan request may be rejected, in which case the complete callback will be
 *       invoked immediately with an error status.
 *
 * @param scan_req          Scan request instance. See @ref mmwlan_scan_req.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_scan_request(const struct mmwlan_scan_req *scan_req);

/**
 * Abort in progress or pending scans.
 *
 * The scan callback will be called back with result code @ref MMWLAN_SCAN_TERMINATED for
 * all aborted scans.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_scan_abort(void);

/**
 * Gets the MAC address of this device.
 *
 * The MAC address address comes from one of the following sources, in descending priority order:
 * 1. @c mmhal_read_mac_addr()
 * 2. Morse Micro transceiver
 * 3. Randomly generated in the form `02:01:XX:XX:XX:XX`
 *
 * @note If a MAC address override is not provided (via @c mmhal_read_mac_addr()) then the
 *       transceiver must have been booted at least once before this function is invoked.
 *
 * @param mac_addr  Buffer to receive the MAC address. Length must be @ref MMWLAN_MAC_ADDR_LEN.
 *
 * @return @ref MMWLAN_SUCCESS on success, @ref MMWLAN_UNAVAILABLE if the MAC address was not
 *         able to be read from the transceiver because it was not booted, else an appropriate
 *         error code.
 */
enum mmwlan_status mmwlan_get_mac_addr(uint8_t *mac_addr);

/**
 * Gets the station's AID
 *
 * @return AID of station, or 0 if not associated
 */
uint16_t mmwlan_get_aid(void);

/**
 * Gets the BSSID of the AP to which the STA is associated.
 *
 * @param bssid  Buffer to receive the BSSID. Length must be @ref MMWLAN_MAC_ADDR_LEN.
 *               Will be set to 00:00:00:00:00:00 if the station is not currently associated.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_get_bssid(uint8_t *bssid);

/**
 * Gets the RSSI measured from the AP.
 *
 * When power save is enabled, this will only be updated when traffic is received from the AP.
 *
 * @returns last known RSSI of the AP or INT32_MIN on error.
 */
int32_t mmwlan_get_rssi(void);

/** @} */

/**
 * @defgroup MMWLAN_OFFLOAD     WLAN offload features
 *
 * @{
 *
 * WLAN offload features enable offloading some high level networking features to the WLAN chip.
 * Features like ARP response, ARP refresh and DHCP lease updates can be offloaded to the chip
 * allowing the host processor to sleep for longer resulting in better power savings.
 *
 */

/**
 * Enables ARP response offload.
 *
 * When enabled the chip will automatically respond to ARP requests with the specified
 * IPv4 address.
 *
 * @note ARP offload is not supported for IPv6 addresses.
 *
 * @param arp_addr  The IPv4 address to respond with for ARP requests for this interface.
 *
 * @returns @ref MMWLAN_SUCCESS on success or @ref MMWLAN_ERROR on failure.
 */
enum mmwlan_status mmwlan_enable_arp_response_offload(uint32_t arp_addr);

/**
 * Enables ARP refresh offload.
 *
 * When enabled the Morse chip will periodically send ARP requests to the AP to refresh
 * its ARP table. This keeps this stations ARP entry from expiring. ARP response offload
 * needs to be enabled first for this feature to work.
 *
 * @note ARP refresh offload is not supported for IPv6 addresses.
 *
 * @param interval_s   The interval in seconds to refresh the ARP entries on the AP.
 * @param dest_ip      The IP to send the ARP packets to.
 * @param send_as_garp If true, send as gratuitous ARP.
 *
 * @returns @ref MMWLAN_SUCCESS on success or @ref MMWLAN_ERROR on failure.
 */
enum mmwlan_status mmwlan_enable_arp_refresh_offload(uint32_t interval_s, uint32_t dest_ip, bool send_as_garp);

/**
 * DHCP lease info structure.
 */
struct mmwlan_dhcp_lease_info {
    /** local IP address */
    uint32_t ip4_addr;
    /** Netmask address */
    uint32_t mask4_addr;
    /** Gateway address */
    uint32_t gw4_addr;
    /** DNS address */
    uint32_t dns4_addr;
};

/** DHCP Lease update callback function prototype - this is called whenever a lease is updated. */
typedef void (*mmwlan_dhcp_lease_update_cb_t)(const struct mmwlan_dhcp_lease_info *lease_info, void *arg);

/**
 * Enables DHCP offload.
 *
 * When enabled the Morse chip will handle DHCP discovery and lease updates automatically.
 *
 * @note DHCP offload is not supported for IPv6 addresses.
 *
 * @note The DHCP lease update callback (@p dhcp_lease_update_cb) must not block and MMWLAN API
 *       functions may not be invoked from the callback.
 *
 * @note Once enabled this feature can only be disabled by a complete system reset or by calling
 *       @c mmwlan_shutdown().
 *
 * @param dhcp_lease_update_cb The callback to call whenever the DHCP lease is updated.
 * @param arg                  An opaque argument to pass to @c dhcp_lease_update_cb.
 *
 * @returns @ref MMWLAN_SUCCESS on success or @ref MMWLAN_ERROR on failure.
 */
enum mmwlan_status mmwlan_enable_dhcp_offload(mmwlan_dhcp_lease_update_cb_t dhcp_lease_update_cb, void *arg);

/**
 * Keep-alive offload configuration options for @ref mmwlan_tcp_keepalive_offload_args.set_cfgs
 * bitmap.
 */
enum mmwlan_tcp_keepalive_offload_cfg {
    /** Bitmap for TCP keep alive period parameter */
    MMWLAN_TCP_KEEPALIVE_SET_CFG_PERIOD = (0x01),
    /** Bitmap for TCP keep alive retry count parameter */
    MMWLAN_TCP_KEEPALIVE_SET_CFG_RETRY_COUNT = (0x02),
    /** Bitmap for TCP keep alive retry interval parameter */
    MMWLAN_TCP_KEEPALIVE_SET_CFG_RETRY_INTERVAL = (0x04),
    /** Bitmap for TCP keep alive source IP parameter */
    MMWLAN_TCP_KEEPALIVE_SET_CFG_SRC_IP_ADDR = (0x08),
    /** Bitmap for TCP keep alive destination IP parameter */
    MMWLAN_TCP_KEEPALIVE_SET_CFG_DEST_IP_ADDR = (0x10),
    /** Bitmap for TCP keep alive source port parameter */
    MMWLAN_TCP_KEEPALIVE_SET_CFG_SRC_PORT = (0x20),
    /** Bitmap for TCP keep alive destination port parameter */
    MMWLAN_TCP_KEEPALIVE_SET_CFG_DEST_PORT = (0x40),
    /** Bitmap for TCP keep alive timing parameters only */
    MMWLAN_TCP_KEEPALIVE_SET_CFG_TIMING_ONLY = (0x07),
    /** Bitmap for all TCP keep alive parameters */
    MMWLAN_TCP_KEEPALIVE_SET_CFG_ALL = (0x7F),
};

/**
 * Arguments data structure for TCP keep-alive arguments.
 *
 * This structure should be initialized using @ref MMWLAN_TCP_KEEPALIVE_OFFLOAD_ARGS_INIT
 * for sensible default values, particularly for forward compatibility with new releases
 * that may add new fields to the struct. For example:
 *
 * @code{.c}
 * struct mmwlan_tcp_keepalive_offload_args args = MMWLAN_TCP_KEEPALIVE_OFFLOAD_ARGS_INIT;
 * @endcode
 */
struct mmwlan_tcp_keepalive_offload_args {
    /**
     * A bitmap specifying which configs to set.
     * See @ref mmwlan_tcp_keepalive_offload_cfg for values.
     */
    uint8_t set_cfgs;
    /**
     * The interval in seconds to send the keep alive in. @c MMWLAN_TCP_KEEPALIVE_SET_CFG_PERIOD
     * bit in @ref set_cfgs must be set if specifying this parameter
     */
    uint16_t period_s;
    /** Number of times to retry before giving up. @c MMWLAN_TCP_KEEPALIVE_SET_CFG_RETRY_COUNT
     * bit in @ref set_cfgs must be set if specifying this parameter
     */
    uint8_t retry_count;
    /** The time to wait between retries in seconds. @c MMWLAN_TCP_KEEPALIVE_SET_CFG_RETRY_INTERVAL
     * bit in @ref set_cfgs must be set if specifying this parameter
     */
    uint8_t retry_interval_s;
    /** The source IP for the keep alive packet. @c MMWLAN_TCP_KEEPALIVE_SET_CFG_SRC_IP_ADDR
     * bit in @ref set_cfgs must be set if specifying this parameter
     */
    uint32_t src_ip;
    /** The source port for the keep alive packet. @c MMWLAN_TCP_KEEPALIVE_SET_CFG_SRC_PORT
     * bit in @ref set_cfgs must be set if specifying this parameter
     */
    uint16_t src_port;
    /** The destination IP for the keep alive packet. @c MMWLAN_TCP_KEEPALIVE_SET_CFG_DEST_IP_ADDR
     * bit in @ref set_cfgs must be set if specifying this parameter
     */
    uint32_t dest_ip;
    /** The destination port for the keep alive packet. @c MMWLAN_TCP_KEEPALIVE_SET_CFG_SRC_PORT
     * bit in @ref set_cfgs must be set if specifying this parameter
     */
    uint16_t dest_port;
};

/**
 * Initializer for @ref mmwlan_tcp_keepalive_offload_args.
 *
 * @see mmwlan_tcp_keepalive_offload_args
 */
#define MMWLAN_TCP_KEEPALIVE_OFFLOAD_ARGS_INIT                                                                                   \
    {                                                                                                                            \
        0                                                                                                                        \
    }

/**
 * Enables TCP keep-alive offload.
 *
 * When enabled the Morse chip will periodically send TCP keep-alive packets to the destination
 * even when the host processor is in standby mode. This function needs to be called before
 * opening a TCP connection.
 *
 * @note TCP keep-alive offload will only be applied to the first TCP connection matching the
 *       given configuration that is opened after this function is called.
 *
 * @note TCP keep-alive offload is not supported for IPv6 addresses.
 *
 * @param args A pointer to arguments of @c struct @c mmwlan_tcp_keepalive_offload_args.
 *
 * @returns @ref MMWLAN_SUCCESS on success or @ref MMWLAN_ERROR on failure.
 */
enum mmwlan_status mmwlan_enable_tcp_keepalive_offload(const struct mmwlan_tcp_keepalive_offload_args *args);

/**
 * Disables the TCP keep-alive offload feature.
 *
 * Has no effect if TCP keep-alive offload is not enabled.
 *
 * @returns @ref MMWLAN_SUCCESS on success or @ref MMWLAN_ERROR on failure.
 */
enum mmwlan_status mmwlan_disable_tcp_keepalive_offload(void);

/**
 * If this bit is set in the flags parameter of @ref mmwlan_config_whitelist then any active
 * whitelist filters are cleared,
 */
#define MMWLAN_WHITELIST_FLAGS_CLEAR 0x01

/**
 * Whitelist filter configuration
 */
struct mmwlan_config_whitelist {
    /** Flags - clear any active whitelist filters if bit 0 is set */
    uint8_t flags;
    /** The IP protocol to match - 6 for TCP, 17 for UDP, 0 for any */
    uint8_t ip_protocol;
    /** The @c LLC protocol to match - 0x0800 for IPv4 and 0x86DD for IPv6, 0 for any */
    uint16_t llc_protocol;
    /** The IPv4 source address to match, 0.0.0.0 for any. */
    uint32_t src_ip;
    /** The IPv4 destination address to match, 0.0.0.0 for any - this is usually our IP address */
    uint32_t dest_ip;
    /** The netmask to apply to the source or destination IP, 0.0.0.0 for any */
    uint32_t netmask;
    /** The source TCP or UDP port to match, 0 for any */
    uint16_t src_port;
    /** The destination TCP or UDP port to match, 0 for any */
    uint16_t dest_port;
};

/**
 * Sets and enables the IP whitelist filter.
 *
 * The IP whitelist filter specifies which incoming IP packets can wake the host from standby.
 * The filter can be used to specify parameters such as source or destination IP addresses to
 * match on, source or destination ports and even IP layer (Such as TCP, ICMP or UDP) or @c LLC
 * layer (Such as IPv4 or IPv6) protocols. If any filter parameter is set to 0, then it is
 * excluded from the filtering process.
 *
 * If the @c flags parameter is set to @c MMWLAN_WHITELIST_FLAGS_CLEAR, then all active whitelist
 * filters are cleared.
 *
 * @note Address/port level filtering is not supported for IPv6 packets.
 *
 * @param whitelist The whitelist filter to set.
 *
 * @returns @ref MMWLAN_SUCCESS on success or @ref MMWLAN_ERROR on failure.
 */
enum mmwlan_status mmwlan_set_whitelist_filter(const struct mmwlan_config_whitelist *whitelist);

/** @} */

/**
 * @defgroup MMWLAN_STANDBY     WLAN Standby features
 *
 * @ingroup MMWLAN_OFFLOAD
 *
 * @{
 *
 * The standby mode allows the system to power off or put the host processor in a deep sleep mode
 * while the Morse chip takes over certain functionality to keep the connection alive with the
 * provision to wake up the host processor when certain conditions are met.
 *
 * When in Standby mode, the chip will:
 * - Not try to communicate with the host in any way.
 * - Use normal Power Save mechanisms to wake for DTIM beacons and to receive data frames when a
 *   DTIM beacon indicates pending data.
 * - Directly respond to ARP requests if ARP offload is enabled, handle DHCP lease expiry/renewal
 *   if DHCP offload is enabled, and drop all other traffic other than a wake request packet from
 *   the access point.
 * - Monitor access point reach-ability and provide status by periodically transmitting encrypted
 *   keep-alive frames to the access point.
 * - If access point is no longer reachable, then the chip will enter a snooze mode where
 *   it stops listening for beacons, waking up only occasionally to see if the access point is
 *   reachable again. This allows the chip to conserve power when the access point is not reachable.
 *
 * The chip will wake the host only if one of the following occurs.
 * - An encrypted Standby wake packet is received.
 * - Association has been lost (indicated via a @c deauth response to a keep-alive frame from the
 *   access point).
 * - The access point was not reachable and beacons are now detected indicating the access point
 *   is available again.
 * - The input trigger GPIO pin on the chip was toggled indicating some on board hardware requires
 *   attention.
 * - A TCP connection maintained by the TCP keep-alive offload feature was lost.
 *
 * The host will be powered on by toggling a GPIO pin, the BUSY pin is also toggled to interrupt
 * the host. By offloading features like ARP response, ARP refresh, TCP keep-alive, and DHCP lease
 * updates while in standby mode allows the host processor to sleep for longer resulting in better
 * power savings.
 */

/**
 * Reasons we can exit standby mode.
 */
enum mmwlan_standby_exit_reason {
    /** No specific reason for exiting standby mode */
    MMWLAN_STANDBY_EXIT_REASON_NONE,
    /** The STA has received the wake-up frame */
    MMWLAN_STANDBY_EXIT_REASON_WAKEUP_FRAME,
    /** The STA needs to (re)associate */
    MMWLAN_STANDBY_EXIT_REASON_ASSOCIATE,
    /** The external input pin has fired */
    MMWLAN_STANDBY_EXIT_REASON_EXT_INPUT,
    /** Whitelisted packet received */
    MMWLAN_STANDBY_EXIT_REASON_WHITELIST_PKT,
    /** TCP connection lost */
    MMWLAN_STANDBY_EXIT_REASON_TCP_CONNECTION_LOST,
};

/**
 * Standby mode callback whenever an event requiring an exit from standby mode occurs.
 *
 * @param reason The reason we exited standby mode. See enum @ref mmwlan_standby_exit_reason.
 * @param arg    An opaque pointer passed from @ref mmwlan_standby_enter()
 */
typedef void (*mmwlan_standby_exit_cb_t)(uint8_t reason, void *arg);

/** Arguments for @ref mmwlan_standby_enter  */
struct mmwlan_standby_enter_args {
    /** Callback function to call when we exit standby mode */
    mmwlan_standby_exit_cb_t standby_exit_cb;
    /** Arguments to pass to callback function */
    void *standby_exit_arg;
};

/**
 * This function puts the Morse chip into standby mode allowing the host processor to go to sleep.
 *
 * When in standby mode the Morse chip takes over certain functionality to keep the connection
 * alive with the provision to wake up the host processor when certain conditions are met.
 * Exit from standby mode can be triggered by the Morse chip under certain conditions or by the host by
 * invoking @ref mmwlan_standby_exit(). Before invoking this function, standby mode parameters can be
 * configured by calling @ref mmwlan_standby_set_config(), @ref mmwlan_standby_set_status_payload() and/or
 * @ref mmwlan_standby_set_wake_filter()
 *
 * @param args   A pointer to the arguments for this function.
 *
 * @returns @ref MMWLAN_SUCCESS on success or @ref MMWLAN_ERROR on failure.
 */
enum mmwlan_status mmwlan_standby_enter(const struct mmwlan_standby_enter_args *args);

/**
 * Forces the Morse chip to exit standby mode.
 *
 * here may be certain instances such as a timer expiry, which cause the host chip to wake up
 * independent of the Morse chip. In such situations, the host calls this function to instruct
 * the Morse chip to exit standby mode and return to normal operating mode.
 *
 * Triggers @ref mmwlan_standby_exit_cb_t with reason @c MMWLAN_STANDBY_EXIT_REASON_NONE.
 *
 * @returns @ref MMWLAN_SUCCESS on success or @ref MMWLAN_ERROR on failure.
 */
enum mmwlan_status mmwlan_standby_exit(void);

/** Arguments for @ref mmwlan_standby_set_status_payload  */
struct mmwlan_standby_set_status_payload_args {
    /** User data to append to the standby status packets */
    uint8_t payload[MMWLAN_STANDBY_STATUS_FRAME_USER_PAYLOAD_MAXLEN];
    /**
     * The length of the payload in bytes.
     * See @ref MMWLAN_STANDBY_STATUS_FRAME_USER_PAYLOAD_MAXLEN for maximum payload length.
     */
    uint32_t payload_len;
};

/**
 * Sets the user payload in the standby status packet.
 *
 * Once standby mode is enabled, the Morse chip will periodically emit a UDP packet of the
 * following format regardless of whether it is in standby or not. The UDP packet
 * will also be sent immediately upon entering or exiting Standby mode.
 *
 * @code
 * +----------------------+----------------------+--------------------------------+---------+
 * | Morse OUI (0c:bf:74) | Type: Standby (0x01) | Awake (0x00) or Standby (0x01) | Payload |
 * +----------------------+----------------------+--------------------------------+---------+
 * @endcode
 *
 * @note The payload is optional and is not present if this function is not called.
 *
 * @param args   A pointer to the arguments for this function.
 *
 * @returns @ref MMWLAN_SUCCESS on success or @ref MMWLAN_ERROR on failure.
 */
enum mmwlan_status mmwlan_standby_set_status_payload(const struct mmwlan_standby_set_status_payload_args *args);

/** Arguments for @ref mmwlan_standby_set_wake_filter  */
struct mmwlan_standby_set_wake_filter_args {
    /** Data to match with for filtering */
    uint8_t filter[MMWLAN_STANDBY_WAKE_FRAME_USER_FILTER_MAXLEN];
    /**
     * The length of the filter data in bytes.
     * See @ref MMWLAN_STANDBY_WAKE_FRAME_USER_FILTER_MAXLEN for maximum filter length.
     */
    uint32_t filter_len;
    /** The offset within the packet to search for the filter match */
    uint32_t offset;
};

/**
 * Configures the standby mode UDP wake packet filter.
 *
 * The system can be woken up from standby mode by sending it a UDP wake packet of the following
 * format. If a wake filter is set using this function then the wake packet will only wake up
 * the system if the specified filter pattern matches the payload at the specified offset within
 * the payload.
 *
 * The wake packet has the following format:
 * @code
 * +----------------------+----------------------+----------------+--------------------+
 * | Morse OUI (0c:bf:74) | Type: Standby (0x01) | Wake up (0x02) | Payload (optional) |
 * +----------------------+----------------------+----------------+--------------------+
 * @endcode
 *
 * @note If a wake filter is not configured then the system will wake on any wake packet and
 *       the payload (if any) is ignored.
 *
 * @param args   A pointer to the arguments for this function.
 *
 * @returns @ref MMWLAN_SUCCESS on success or @ref MMWLAN_ERROR on failure.
 */
enum mmwlan_status mmwlan_standby_set_wake_filter(const struct mmwlan_standby_set_wake_filter_args *args);

/**
 * Standby mode configuration parameters. If the @ref mmwlan_standby_set_config()
 * function is not called, then the defaults are as specified.
 */
struct mmwlan_standby_config {
    /** Interval in seconds for transmitting Standby status packets. (Default 15s) */
    uint32_t notify_period_s;
    /** Source IP address to use for the standby status packets. (Default 0.0.0.0) */
    uint32_t src_ip;
    /** Destination IP address for the standby status packets. (Default 0.0.0.0) */
    uint32_t dst_ip;
    /**
     * Destination UDP Port for the standby status packets, also used the source port for
     * outgoing UDP port for outgoing UDP packets. (Default 22000)
     */
    uint16_t dst_port;
    /**
     * The interval in seconds to wait after beacon loss before entering snooze. In
     * snooze mode the chip stops listening for beacons to save power. (Default 120s)
     */
    uint32_t bss_inactivity_before_snooze_s;
    /**
     * The interval in seconds to wake periodically from snooze and check for beacons.
     * If no beacons are found then the chip will re-enter snooze. If beacons are found
     * then the chip will exit standby mode so the host can re-associate. (Default 60s)
     */
    uint32_t snooze_period_s;
    /**
     * The amount in seconds to increase successive snooze intervals. This saves power
     * by sleeping for longer before checking for beacons again if no beacons are found.
     * (Default 0s)
     */
    uint32_t snooze_increment_s;
    /** The maximum time in seconds to snooze for after increments. (Default @c UINT32_MAX) */
    uint32_t snooze_max_s;
};

/**
 * Sets the configuration for standby mode.
 *
 * @param config A pointer to the configuration structure. See @ref mmwlan_standby_config.
 *
 * @returns @ref MMWLAN_SUCCESS on success or @ref MMWLAN_ERROR on failure.
 */
enum mmwlan_status mmwlan_standby_set_config(const struct mmwlan_standby_config *config);

/** @} */

/**
 * @defgroup MMWLAN_WNM     WNM Sleep management
 *
 * @{
 *
 * @note It is highly recommended to refer to Morse Micro App Note 04 on WNM sleep for a more
 *       detailed explanation on how this feature works.
 *
 * WNM sleep is an extended power save mode in which the STA need not listen for every DTIM beacon
 * and need not perform rekey, allowing the STA to remain associated and reduce power consumption
 * when it has no traffic to send or receive from the AP. While a STA is in WNM sleep it will be
 * unable to communicate with the AP, and thus any traffic for the STA may be buffered or dropped
 * at the discretion of the AP while the STA is in WNM sleep.
 *
 * @note WNM sleep can only be entered after the STA is associated to the AP. The AP must also
 *       support WNM sleep.
 *
 * When entry to WNM sleep is enabled via this API, the STA sends a request to the AP and after
 * successful negotiation, the chip is either put into a lower power mode or powered down
 * completely, depending on arguments given when WNM sleep mode was enabled. The datapath is paused
 * during this time. If the STA does not successfully negotiate with the AP to enter WNM Sleep, it
 * will return an error code and the chip will not enter a low power mode.
 *
 * When WNM sleep is disabled, the chip returns to normal operation and sends a request to the AP
 * to exit WNM sleep. Note that the chip will exit low power mode regardless of whether or not the
 * exit request was successful. WNM sleep will be exited if @ref mmwlan_shutdown or
 * @ref mmwlan_sta_disable is invoked.
 *
 * If the AP takes down the link, then the station will be unaware until it sends the WNM Sleep
 * exit request. At that point the AP will send a de-authentication frame with reason code
 * "non-associated station". After validating this is actually the AP the station was connected
 * to, the station will bring down the connection and return @ref MMWLAN_ERROR. The station will
 * attempt to re-associate but will not re-enter WNM sleep. The application needs to enable WNM
 * sleep again via this API if required.
 *
 * A high level overview of enabling and disabling WNM sleep is shown below.
 *
 * @include{doc} wnm_sleep_overview.puml
 */

/** Structure for storing WNM sleep extended arguments. */
struct mmwlan_set_wnm_sleep_enabled_args {
    /** Boolean indicating whether WNM sleep is enabled. */
    bool wnm_sleep_enabled;
    /** Boolean indicating whether chip should be powered down during WNM sleep. */
    bool chip_powerdown_enabled;
};

/**
 * Initializer for @ref mmwlan_set_wnm_sleep_enabled_args.
 *
 * For example:
 *
 * @code{c}
 * struct mmwlan_set_wnm_sleep_enabled_args wnm_sleep_args = MMWLAN_SET_WNM_SLEEP_ENABLED_ARGS_INIT;
 * @endcode
 */
#define MMWLAN_SET_WNM_SLEEP_ENABLED_ARGS_INIT                                                                                   \
    {                                                                                                                            \
        false, false                                                                                                             \
    }

/**
 * Sets extended WNM sleep mode.
 *
 * Provides an extended interface for setting WNM sleep. See @ref mmwlan_set_wnm_sleep_enabled_args
 * for parameter details. If WNM sleep mode is enabled then the transceiver will sleep across
 * multiple DTIM periods until WNM sleep mode is disabled. This allows the transceiver to use less
 * power with the caveat that it will not wake up for group- or individually-addressed traffic. If
 * a group rekey occurs while the device is in WNM sleep it will be applied when the device exits
 * WNM sleep.
 *
 * @note Data should not be queued for transmission (using, e.g., @ref mmwlan_tx())  during
 *       WNM sleep.
 * @note 802.11 power save must be enabled for any benefit to be obtained
 *       (see @ref mmwlan_set_power_save_mode()).
 * @note Negotiation with the AP is required to enter WNM sleep. As such the transceiver will only
 *       be placed into low power mode when a connection has been establish and the AP has accepted
 *       the request to enter WNM sleep. This will automatically be handled within mmwlan once WNM
 *       sleep mode is enabled.
 *
 * @param args  WNM sleep arguments - see @ref mmwlan_set_wnm_sleep_enabled_args.
 *
 * @return @ref MMWLAN_SUCCESS on success, MMWLAN_UNAVAILABLE if already requested,
 *              else an appropriate error code.
 *         @ref MMWLAN_TIMED_OUT if the maximum retry request reached. For WNM sleep exit request,
 *              this means that the device exited WNM sleep but failed to inform the AP.
 */
enum mmwlan_status mmwlan_set_wnm_sleep_enabled_ext(const struct mmwlan_set_wnm_sleep_enabled_args *args);

/**
 * Sets whether WNM sleep mode is enabled.
 *
 * If WNM sleep mode is enabled then the transceiver will sleep across multiple DTIM periods
 * until WNM sleep mode is disabled. This allows the transceiver to use less power with the
 * caveat that it will not wake up for group- or individually-addressed traffic. If a group
 * rekey occurs while the device is in WNM sleep it will be applied when the device exits
 * WNM sleep.
 *
 * @note Data should not be queued for transmission (using, e.g., @ref mmwlan_tx())  during
 *       WNM sleep.
 * @note 802.11 power save must be enabled for any benefit to be obtained
 *       (see @ref mmwlan_set_power_save_mode()).
 * @note Negotiation with the AP is required to enter WNM sleep. As such the transceiver will only
 *       be placed into low power mode when a connection has been establish and the AP has accepted
 *       the request to enter WNM sleep. This will automatically be handled within mmwlan once WNM
 *       sleep mode is enabled.
 *
 * @param wnm_sleep_enabled   Boolean indicating whether WNM sleep is enabled.
 *
 * @return @ref MMWLAN_SUCCESS on success, MMWLAN_UNAVAILABLE if already requested,
 *              else an appropriate error code.
 *         @ref MMWLAN_TIMED_OUT if the maximum retry request reached. For WNM sleep exit request,
 *              this means that the device exited WNM sleep but failed to inform the AP.
 */
static inline enum mmwlan_status mmwlan_set_wnm_sleep_enabled(bool wnm_sleep_enabled)
{
    struct mmwlan_set_wnm_sleep_enabled_args wnm_sleep_args = MMWLAN_SET_WNM_SLEEP_ENABLED_ARGS_INIT;
    wnm_sleep_args.wnm_sleep_enabled = wnm_sleep_enabled;
    return mmwlan_set_wnm_sleep_enabled_ext(&wnm_sleep_args);
}

/**
 * @}
 */

/**
 * @defgroup MMWLAN_BEACON_VENDOR_IE_FILTER_API     Beacon Vendor Specific IE Filter API
 *
 * @{
 *
 * This API enables access to Vendor Specific information elements (IEs) in beacons. The application
 * can register a callback to be executed if Vendor Specific IEs containing one of the specified
 * Organizational Unique Identifiers (OUIs). Up to @ref MMWLAN_BEACON_VENDOR_IE_MAX_OUI_FILTERS can
 * be specified.
 */

/** Max number of OUIs supported in vendor IE OUI filter, @ref mmwlan_beacon_vendor_ie_filter. */
#define MMWLAN_BEACON_VENDOR_IE_MAX_OUI_FILTERS (5)

/**
 * Beacon vendor ie filter callback function prototype.
 *
 * @param ies       Reference to the list of information elements (little endian order) present in
 *                  the beacon that matched the filter.
 * @param ies_len   Length of the IE list in octets.
 * @param arg       Reference to the opaque argument provided with the filter.
 */
typedef void (*mmwlan_beacon_vendor_ie_filter_cb_t)(const uint8_t *ies, uint32_t ies_len, void *arg);

/** 24-bit OUI type. */
typedef uint8_t mmwlan_oui_t[MMWLAN_OUI_SIZE];

/** Structure for storing beacon vendor ie filter parameter. */
struct mmwlan_beacon_vendor_ie_filter {
    /**
     * Callback that will be executed upon reception of a beacon containing a Vendor Specific
     * information element that has an OUI that matches this filter. Must not be @c NULL.
     *
     * @note This is executed in the MMWLAN main processing loop. As such, processing within the
     * callback itself should be kept as short as possible. If extensive processing is done within
     * the callback this could have an impact on throughput and/or connection stability.
     */
    mmwlan_beacon_vendor_ie_filter_cb_t cb;
    /** Opaque argument to be passed to the callbacks. */
    void *cb_arg;
    /** Number of OUIs contained within @ref ouis. This can be @ref
     * MMWLAN_BEACON_VENDOR_IE_MAX_OUI_FILTERS max. */
    uint8_t n_ouis;
    /** List of OUIs to filter on. */
    mmwlan_oui_t ouis[MMWLAN_BEACON_VENDOR_IE_MAX_OUI_FILTERS];
};

/**
 * Function to update the filter used to pass beacon back.
 *
 * @note This will block until the filter has been installed or some error has occurred.
 *
 * @param filter  Reference to the filter to use. This must reside in memory and not be modified
 *                until after the filter is **successfully** cleared. To clear the filter pass @c
 *                NULL here.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_update_beacon_vendor_ie_filter(const struct mmwlan_beacon_vendor_ie_filter *filter);

/** @} */

/*
 * ---------------------------------------------------------------------------------------------
 */

/**
 * @defgroup MMWLAN_INIT    WLAN Initialization/Deinitialization API
 *
 * @note If using LWIP, this API is invoked by the LWIP @c netif for MMWLAN, and thus does not
 *       need to be used directly by the application.
 *
 * @{
 */

/**
 * Initialize the MMWLAN subsystem.
 *
 * @warning @ref mmhal_init() must be called before this function is executed.
 */
void mmwlan_init(void);

/**
 * Deinitialize the MMWLAN subsystem, freeing any allocated memory.
 *
 * Must not be called while there is any activity in progress (e.g., while connected).
 *
 * @warning @ref mmwlan_shutdown must be called before executing this function.
 */
void mmwlan_deinit(void);

/** @} */

/**
 * @defgroup MMWLAN_HEALTH    WLAN Health check API
 *
 * @{
 */

/**
 * The default minimum interval to wait after the last health check before triggering another.
 * If this is 0 then health checks will always happen at the
 * @ref MMWLAN_DEFAULT_MAX_HEALTH_CHECK_INTERVAL_MS value.
 * @ref MMWLAN_DEFAULT_MIN_HEALTH_CHECK_INTERVAL_MS must always be less than or equal to
 * @ref MMWLAN_DEFAULT_MAX_HEALTH_CHECK_INTERVAL_MS.
 */
#define MMWLAN_DEFAULT_MIN_HEALTH_CHECK_INTERVAL_MS 60000

/**
 * The default maximum interval to wait after the last health check before triggering another.
 * If this parameter is 0 then periodic health checks will be disabled.
 * @ref MMWLAN_DEFAULT_MIN_HEALTH_CHECK_INTERVAL_MS must always be less than or equal to
 * @ref MMWLAN_DEFAULT_MAX_HEALTH_CHECK_INTERVAL_MS. Set this to @c UINT32_MAX to have the
 * maximum unbounded.
 */
#define MMWLAN_DEFAULT_MAX_HEALTH_CHECK_INTERVAL_MS 120000

/**
 * Specify the periodic health check intervals.
 *
 * The system will always schedule the periodic health check for the @c max_interval_ms
 * interval. It will however monitor system activity and opportunistically perform health
 * checks after @c min_interval_ms time has passed and some activity was detected. If no
 * activity was detected then the health check will be performed after @c max_interval_ms
 * as originally scheduled. Every time the health check is performed, the next one will be
 * rescheduled to occur after @c max_interval_ms. If both @c min_interval_ms and
 * @c max_interval_ms are set to 0, then periodic health checks will be disabled.
 *
 * @param min_interval_ms  The minimum interval to wait after the last health check before
 *                         triggering another. If this parameter is 0 then health checks will
 *                         always happen at the @c max_interval_ms value. @c min_interval_ms
 *                         must always be less than or equal to @c max_interval_ms.
 *
 * @param max_interval_ms  The maximum interval to wait after the last health check before
 *                         triggering another. If this parameter is 0 then periodic health checks
 *                         will be disabled. @c min_interval_ms must always be less than or
 *                         equal to @c max_interval_ms. Set this to @c UINT32_MAX to have the
 *                         maximum unbounded.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_set_health_check_interval(uint32_t min_interval_ms, uint32_t max_interval_ms);

/** @} */

/*
 * ---------------------------------------------------------------------------------------------
 */

/**
 * @defgroup MMWLAN_DATA    WLAN Datapath API
 *
 * @{
 */

/** Enumeration of link states. */
enum mmwlan_link_state {
    MMWLAN_LINK_DOWN, /**< The link is down. */
    MMWLAN_LINK_UP,   /**< The link is up. */
};

/**
 * Prototype for link state change callbacks.
 *
 * @param link_state    The new link state.
 * @param arg           Opaque argument that was given when the callback was registered.
 */
typedef void (*mmwlan_link_state_cb_t)(enum mmwlan_link_state link_state, void *arg);

/**
 * Register a link status callback.
 *
 * @note Only one link status callback may be registered. Further registration will overwrite the
 *       previously registered callback.
 *
 * @note The link status callback must not block and MMWLAN API functions may not be invoked
 *       from the callback.
 *
 * @param callback  The callback to register.
 * @param arg       Opaque argument to be passed to the callback.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_register_link_state_cb(mmwlan_link_state_cb_t callback, void *arg);

/**
 * Receive data packet callback function.
 *
 * @param header        Buffer containing the 802.3 header for this packet.
 * @param header_len    Length of the @p header.
 * @param payload       Packet payload (excluding header).
 * @param payload_len   Length of @p payload.
 * @param arg           Opaque argument that was given when the callback was registered.
 */
typedef void (*mmwlan_rx_cb_t)(uint8_t *header, unsigned header_len, uint8_t *payload, unsigned payload_len, void *arg);

/**
 * Register a receive callback.
 *
 * @note Only one receive callback may be registered. Further registration will overwrite the
 *       previously registered callback.
 *
 * @note Only a single receive callback may be registered at a time.
 *
 * @param callback  The callback to register (@c NULL to unregister).
 * @param arg       Opaque argument to be passed to the callback.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_register_rx_cb(mmwlan_rx_cb_t callback, void *arg);

/**
 * Receive data packet callback function, consuming an mmpkt.
 *
 * @param mmpkt  The mmpkt containing the received packet, including an 802.3 header.
 *               Ownership of the mmpkt is passed to this callback.
 * @param arg    Opaque argument that was given when the callback was registered.
 */
typedef void (*mmwlan_rx_pkt_cb_t)(struct mmpkt *mmpkt, void *arg);

/**
 * Register a receive callback which consumes an mmpkt.
 *
 * @note Only one receive callback of any type may be registered. Further registration will
 *       overwrite the previously registered callback.
 *
 * @param callback  The callback to register (@c NULL to unregister).
 * @param arg       Opaque argument to be passed to the callback.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_register_rx_pkt_cb(mmwlan_rx_pkt_cb_t callback, void *arg);

/**
 * Blocks until the transmit path is ready for transmit.
 *
 * @param timeout_ms    The maximum time to wait, in milliseconds. If zero then this function
 *                      does not block.
 *
 * @return @ref MMWLAN_SUCCESS on success, @ref MMWLAN_TIMED_OUT if the transmit datapath
 *         was not ready within the given timeout, or another error code as appropriate.
 */
enum mmwlan_status mmwlan_tx_wait_until_ready(uint32_t timeout_ms);

/**
 * Allocate an @c mmpkt data structure for transmission with at least enough space for the given
 * payload length.
 *
 * The return mmpkt can be passed to @ref mmwlan_tx_pkt().
 *
 * @param payload_len   Minimum space required for payload.
 * @param tid           The TID that this packet will be transmitted at. This may be used
 *                      by the allocation function to, for example, prioritize allocation of
 *                      certain classes of traffic.
 *
 * @returns the allocated mmpkt on success or @c NULL on failure.
 */
struct mmpkt *mmwlan_alloc_mmpkt_for_tx(uint32_t payload_len, uint8_t tid);

/** Default transmit timeout. Used by @ref mmwlan_tx() and @ref mmwlan_tx_tid().  */
#define MMWLAN_TX_DEFAULT_TIMEOUT_MS (1000)

/** Default QoS Traffic ID (TID) to use for transmit (@c mmwlan_tx()). */
#define MMWLAN_TX_DEFAULT_QOS_TID (0)

/** Maximum Traffic ID (TID) supported for QoS traffic. */
#define MMWLAN_MAX_QOS_TID (7)

/**
 * Metadata for TX packets.
 *
 * This structure should be initialized using @ref MMWLAN_TX_METADATA_INIT for sensible
 * default values, particularly for forward compatibility with new releases that may add
 * new fields to the struct. For example:
 *
 * @code{.c}
 *     enum mmwlan_status status;
 *     struct mmwlan_tx_metadata metadata = MMWLAN_TX_METADATA_INIT;
 *     // HERE: initialize metadata
 *     status = mmwlan_tx_pkt(pkt, &metadata);
 * @endcode
 */
struct mmwlan_tx_metadata {
    /**
     * Traffic ID (TID) to use. Must be less than or equal to @ref MMWLAN_MAX_QOS_TID.
     * @see MMWLAN_TX_DEFAULT_QOS_TID
     */
    uint8_t tid;
};

/**
 * Initializer for @ref mmwlan_tx_metadata.
 */
#define MMWLAN_TX_METADATA_INIT                                                                                                  \
    {                                                                                                                            \
        MMWLAN_TX_DEFAULT_QOS_TID                                                                                                \
    }

/**
 * Transmit the given packet. The packet must start with an 802.3 header, which will be
 * translated into an 802.11 header by this function.
 *
 * @code
 *
 *    +----------+----------+-----------+------------------+
 *    | DST ADDR | SRC ADDR | ETHERTYPE |   Payload        |
 *    +----------+----------+-----------+------------------+
 *    ^                                 ^                  ^
 *    |--------802.3 MAC Header---------|                  |
 *    |                                                    |
 *    |                                                    |
 *    |                                                    |
 *    |<----------------------len------------------------->|
 *  data
 *
 * @endcode
 *
 * @note The TID should be set in the tx metadata of @p txbuf before invoking this function.
 *
 * @note This function is non-blocking. It will return immediately if the tx path is blocked.
 *       Use the tx flow control callback.
 *
 * @warning The given @p txbuf must be allocated by @ref mmwlan_alloc_mmpkt_for_tx().
 *
 * @param pkt       mmpkt containing the packet to transmit. This will be consumed by this
 *                  function.
 * @param metadata  Extra information relating to the packet transmission. May be @c NULL,
 *                  in which case default values will be used.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_tx_pkt(struct mmpkt *pkt, const struct mmwlan_tx_metadata *metadata);

/**
 * Transmit the given packet using the given QoS Traffic ID (TID). The packet must start with
 * an 802.3 header that will be translated into an 802.11 header.
 *
 * @code
 *
 *    +----------+----------+-----------+------------------+
 *    | DST ADDR | SRC ADDR | ETHERTYPE |   Payload        |
 *    +----------+----------+-----------+------------------+
 *    ^                                 ^                  ^
 *    |--------802.3 MAC Header---------|                  |
 *    |                                                    |
 *    |                                                    |
 *    |                                                    |
 *    |<----------------------len------------------------->|
 *  data
 *
 * @endcode
 *
 * @param data  Packet data.
 * @param len   Length of packet.
 * @param tid   TID to use (0 - @ref MMWLAN_MAX_QOS_TID).
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
static inline enum mmwlan_status mmwlan_tx_tid(const uint8_t *data, unsigned len, uint8_t tid)
{
    enum mmwlan_status status;
    struct mmpkt *pkt;
    struct mmpktview *pktview;
    struct mmwlan_tx_metadata metadata = MMWLAN_TX_METADATA_INIT;

    status = mmwlan_tx_wait_until_ready(MMWLAN_TX_DEFAULT_TIMEOUT_MS);
    if (status != MMWLAN_SUCCESS) {
        return status;
    }

    pkt = mmwlan_alloc_mmpkt_for_tx(len, tid);
    if (pkt == NULL) {
        return MMWLAN_NO_MEM;
    }

    pktview = mmpkt_open(pkt);
    mmpkt_append_data(pktview, data, len);
    mmpkt_close(&pktview);

    metadata.tid = tid;
    return mmwlan_tx_pkt(pkt, &metadata);
}

/**
 * Transmit the given packet using @ref MMWLAN_TX_DEFAULT_QOS_TID. The packet must start with an
 * 802.3 header that will be translated into an 802.11 header.
 *
 * @code
 *
 *    +----------+----------+-----------+------------------+
 *    | DST ADDR | SRC ADDR | ETHERTYPE |   Payload        |
 *    +----------+----------+-----------+------------------+
 *    ^                                 ^                  ^
 *    |--------802.3 MAC Header---------|                  |
 *    |                                                    |
 *    |                                                    |
 *    |                                                    |
 *    |<----------------------len------------------------->|
 *  data
 *
 * @endcode
 *
 * @param data  Packet data.
 * @param len   Length of packet.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
static inline enum mmwlan_status mmwlan_tx(const uint8_t *data, unsigned len)
{
    return mmwlan_tx_tid(data, len, MMWLAN_TX_DEFAULT_QOS_TID);
}

/**
 * Enumeration of states that can be returned by the transmit flow control callback (as
 * registered by @ref mmwlan_register_tx_flow_control_cb().
 */
enum mmwlan_tx_flow_control_state {
    MMWLAN_TX_READY,  /**< Transmit data path ready for packets (not paused). */
    MMWLAN_TX_PAUSED, /**< Transmit data path paused (blocked). */
};

/**
 * Transmit flow control callback function type. When registered, this callback will be
 * invoked when the transmit data path is paused and when unpaused.
 *
 * @note This function will always be invoked from the Upper MAC thread context. Therefore its
 *       invocation may not be synchronous with changes in flow control state.
 *
 * @param state         Current transmit flow control state.
 * @param arg           Opaque argument that was given when the function was registered.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
typedef void (*mmwlan_tx_flow_control_cb_t)(enum mmwlan_tx_flow_control_state state, void *arg);

/**
 * Register a transmit flow control callback. This callback will be invoked when the tx data path
 * is paused and when unpaused.
 *
 * @note This function will always be invoked from the Upper MAC thread context. Therefore its
 *       invocation may not be synchronous with changes in flow control state.
 *
 * @param cb    The callback to register.
 * @param arg   Opaque argument to pass to the callback.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_register_tx_flow_control_cb(mmwlan_tx_flow_control_cb_t cb, void *arg);

/** @} */

/*
 * ---------------------------------------------------------------------------------------------
 */

/**
 * @defgroup MMWLAN_STATS    Statistics API
 *
 * API for retrieving statistics information from the WLAN subsystem.
 *
 * @{
 */

/** Rate control statistics data structure. */
struct mmwlan_rc_stats {
    /** The number of rate table entries. */
    uint32_t n_entries;
    /** Rate info for each rate table entry. */
    uint32_t *rate_info;
    /** Total number of packets sent for each rate table entry. */
    uint32_t *total_sent;
    /** Total successes for each rate table entry. */
    uint32_t *total_success;
};

/**
 * Enumeration defined offsets into the bit field of rate information (@c rate_info in
 * @ref mmwlan_rc_stats).
 *
 * ```
 * 31         9       8      4    0
 * +----------+-------+------+----+
 * | Reserved | Guard | Rate | BW |
 * |    23    |   1   |  4   | 4  |
 * +----------+-------+------+----+
 * ```
 * * BW: 0 = 1 MHz, 1 = 2 MHz, 2 = 4 MHz
 * * Rate: MCS Rate
 * * Guard: 0 = LGI, 1 = SGI
 */
enum mmwlan_rc_stats_rate_info_offsets {
    MMWLAN_RC_STATS_RATE_INFO_BW_OFFSET = 0,
    MMWLAN_RC_STATS_RATE_INFO_RATE_OFFSET = 4,
    MMWLAN_RC_STATS_RATE_INFO_GUARD_OFFSET = 8,
};

/**
 * Retrieves WLAN rate control statistics.
 *
 * @note The returned data structure must be freed using @ref mmwlan_free_rc_stats().
 *
 * @returns the statistics data structure on success or @c NULL on failure.
 */
struct mmwlan_rc_stats *mmwlan_get_rc_stats(void);

/**
 * Free a mmwlan_rc_stats structure that was allocated with @ref mmwlan_get_rc_stats().
 *
 * @param stats     The structure to be freed (may be @c NULL).
 */
void mmwlan_free_rc_stats(struct mmwlan_rc_stats *stats);

/**
 * Data structure used to represent an opaque buffer containing Morse statistics. This is
 * returned by @c mmwlan_get_morse_stats() and must be freed by @c mmwlan_free_morse_stats().
 */
struct mmwlan_morse_stats {
    /** Buffer containing the stats. */
    uint8_t *buf;
    /** Length of stats in @c buf. */
    uint32_t len;
};

/**
 * Retrieves statistics from the Morse transceiver. The stats are returned as a binary blob
 * that can be parsed by host tools.
 *
 * @param core_num  The core to retrieve stats for.
 * @param reset     Boolean indicating whether to reset the stats after retrieving.
 *
 * @note The returned @c mmwlan_morse_stats instance must be freed using
 *       @ref mmwlan_free_morse_stats.
 *
 * @returns a @c mmwlan_morse_stats instance on success or @c NULL on failure.
 */
struct mmwlan_morse_stats *mmwlan_get_morse_stats(uint32_t core_num, bool reset);

/**
 * Frees a @c mmwlan_morse_stats instance that was returned by @c mmwlan_get_morse_stats().
 *
 * @param stats     The instance to free. May be @ NULL.
 */
void mmwlan_free_morse_stats(struct mmwlan_morse_stats *stats);

/** @} */

/*
 * ---------------------------------------------------------------------------------------------
 */

/**
 * @defgroup MMWLAN_TEST    Test (ATE) API
 *
 * Extended API particularly intended for test use cases.
 *
 * @{
 */

/** Enumeration of MCS rates. */
enum mmwlan_mcs {
    MMWLAN_MCS_NONE = -1,         /**< Use-case specific special value */
    MMWLAN_MCS_0 = 0,             /**< MCS0 */
    MMWLAN_MCS_1,                 /**< MCS1 */
    MMWLAN_MCS_2,                 /**< MCS2 */
    MMWLAN_MCS_3,                 /**< MCS3*/
    MMWLAN_MCS_4,                 /**< MCS4 */
    MMWLAN_MCS_5,                 /**< MCS5 */
    MMWLAN_MCS_6,                 /**< MCS6 */
    MMWLAN_MCS_7,                 /**< MCS7 */
    MMWLAN_MCS_MAX = MMWLAN_MCS_7 /**< Maximum supported MCS rate */
};

/** Enumeration of bandwidths. */
enum mmwlan_bw {
    MMWLAN_BW_NONE = -1,            /**< Use-case specific special value */
    MMWLAN_BW_1MHZ = 1,             /**< 1 MHz bandwidth */
    MMWLAN_BW_2MHZ = 2,             /**< 2 MHz bandwidth */
    MMWLAN_BW_4MHZ = 4,             /**< 4 MHz bandwidth */
    MMWLAN_BW_8MHZ = 8,             /**< 8 MHz bandwidth */
    MMWLAN_BW_MAX = MMWLAN_BW_8MHZ, /**< Maximum supported bandwidth */
};

/** Enumeration of guard intervals. */
enum mmwlan_gi {
    MMWLAN_GI_NONE = -1,            /**< Use-case specific special value */
    MMWLAN_GI_SHORT = 0,            /**< Short guard interval */
    MMWLAN_GI_LONG,                 /**< Long guard interval */
    MMWLAN_GI_MAX = MMWLAN_GI_LONG, /**< Maximum valid value of this @c enum. */
};

/**
 * Enable/disable override of rate control parameters.
 *
 * @param tx_rate_override      Overrides the transmit MCS rate. Set to @ref MMWLAN_MCS_NONE for no
 *                              override.
 * @param bandwidth_override    Overrides the TX bandwidth. Set to @ref MMWLAN_BW_NONE for no
 *                              override.
 * @param gi_override           Overrides the guard interval. Set to @ref MMWLAN_GI_NONE for no
 *                              override.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_ate_override_rate_control(enum mmwlan_mcs tx_rate_override, enum mmwlan_bw bandwidth_override,
                                                    enum mmwlan_gi gi_override);

/**
 * Execute a test/debug command. The format of command and response is opaque to this API.
 *
 * @param[in]     command       Buffer containing the command to be executed. Note that buffer
 *                              contents may be modified by this function.
 * @param[in]     command_len   Length of the command to be executed.
 * @param[out]    response      Buffer to received the response to the command.
 *                              May be NULL, in which case @p response_len should also be NULL.
 * @param[in,out] response_len  Pointer to a @c uint32_t that is initialized to the length of the
 *                              response buffer. On success, the value will be updated to the
 *                              length of data that was put into the response buffer.
 *                              May be NULL, in which case @p response must also be NULL.
 *
 * @returns @c MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_ate_execute_command(uint8_t *command, uint32_t command_len, uint8_t *response, uint32_t *response_len);

/** @} */

#ifdef __cplusplus
}
#endif

/** @} */
