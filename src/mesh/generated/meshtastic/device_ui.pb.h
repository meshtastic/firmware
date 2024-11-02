/* Automatically generated nanopb header */
/* Generated by nanopb-0.4.9 */

#ifndef PB_MESHTASTIC_MESHTASTIC_DEVICE_UI_PB_H_INCLUDED
#define PB_MESHTASTIC_MESHTASTIC_DEVICE_UI_PB_H_INCLUDED
#include <pb.h>

#if PB_PROTO_HEADER_VERSION != 40
#error Regenerate this file with the current version of nanopb generator.
#endif

/* Enum definitions */
typedef enum _meshtastic_Theme {
    /* Dark */
    meshtastic_Theme_DARK = 0,
    /* Light */
    meshtastic_Theme_LIGHT = 1,
    /* Red */
    meshtastic_Theme_RED = 2
} meshtastic_Theme;

/* Localization */
typedef enum _meshtastic_Language {
    /* English */
    meshtastic_Language_ENGLISH = 0,
    /* French */
    meshtastic_Language_FRENCH = 1,
    /* German */
    meshtastic_Language_GERMAN = 2,
    /* Italian */
    meshtastic_Language_ITALIAN = 3,
    /* Portuguese */
    meshtastic_Language_PORTUGUESE = 4,
    /* Spanish */
    meshtastic_Language_SPANISH = 5,
    /* Swedish */
    meshtastic_Language_SWEDISH = 6,
    /* Finnish */
    meshtastic_Language_FINNISH = 7,
    /* Polish */
    meshtastic_Language_POLISH = 8,
    /* Turkish */
    meshtastic_Language_TURKISH = 9,
    /* Serbian */
    meshtastic_Language_SERBIAN = 10,
    /* Russian */
    meshtastic_Language_RUSSIAN = 11,
    /* Dutch */
    meshtastic_Language_DUTCH = 12,
    /* Greek */
    meshtastic_Language_GREEK = 13,
    /* Simplified Chinese (experimental) */
    meshtastic_Language_SIMPLIFIED_CHINESE = 30,
    /* Traditional Chinese (experimental) */
    meshtastic_Language_TRADITIONAL_CHINESE = 31
} meshtastic_Language;

/* Struct definitions */
typedef struct _meshtastic_NodeFilter {
    /* Filter unknown nodes */
    bool unknown_switch;
    /* Filter offline nodes */
    bool offline_switch;
    /* Filter nodes w/o public key */
    bool public_key_switch;
    /* Filter based on hops away */
    int8_t hops_away;
    /* Filter nodes w/o position */
    bool position_switch;
    /* Filter nodes by matching name string */
    char node_name[16];
} meshtastic_NodeFilter;

typedef struct _meshtastic_NodeHighlight {
    /* Highlight nodes w/ active chat */
    bool chat_switch;
    /* Highlight nodes w/ position */
    bool position_switch;
    /* Highlight nodes w/ telemetry data */
    bool telemetry_switch;
    /* Highlight nodes w/ iaq data */
    bool iaq_switch;
    /* Highlight nodes by matching name string */
    char node_name[16];
} meshtastic_NodeHighlight;

typedef struct _meshtastic_DeviceUIConfig {
    /* A version integer used to invalidate saved files when we make incompatible changes. */
    uint32_t version;
    /* TFT display brightness 1..255 */
    uint8_t screen_brightness;
    /* Screen timeout 0..900 */
    uint16_t screen_timeout;
    /* Screen/Settings lock enabled */
    bool screen_lock;
    bool settings_lock;
    uint32_t pin_code;
    /* Color theme */
    meshtastic_Theme theme;
    /* Audible message, banner and ring tone */
    bool alert_enabled;
    bool banner_enabled;
    uint8_t ring_tone_id;
    /* Localization */
    meshtastic_Language language;
    /* Node list filter */
    bool has_node_filter;
    meshtastic_NodeFilter node_filter;
    /* Node list highlighting */
    bool has_node_highlight;
    meshtastic_NodeHighlight node_highlight;
} meshtastic_DeviceUIConfig;


#ifdef __cplusplus
extern "C" {
#endif

/* Helper constants for enums */
#define _meshtastic_Theme_MIN meshtastic_Theme_DARK
#define _meshtastic_Theme_MAX meshtastic_Theme_RED
#define _meshtastic_Theme_ARRAYSIZE ((meshtastic_Theme)(meshtastic_Theme_RED+1))

#define _meshtastic_Language_MIN meshtastic_Language_ENGLISH
#define _meshtastic_Language_MAX meshtastic_Language_TRADITIONAL_CHINESE
#define _meshtastic_Language_ARRAYSIZE ((meshtastic_Language)(meshtastic_Language_TRADITIONAL_CHINESE+1))

#define meshtastic_DeviceUIConfig_theme_ENUMTYPE meshtastic_Theme
#define meshtastic_DeviceUIConfig_language_ENUMTYPE meshtastic_Language




/* Initializer values for message structs */
#define meshtastic_DeviceUIConfig_init_default   {0, 0, 0, 0, 0, 0, _meshtastic_Theme_MIN, 0, 0, 0, _meshtastic_Language_MIN, false, meshtastic_NodeFilter_init_default, false, meshtastic_NodeHighlight_init_default}
#define meshtastic_NodeFilter_init_default       {0, 0, 0, 0, 0, ""}
#define meshtastic_NodeHighlight_init_default    {0, 0, 0, 0, ""}
#define meshtastic_DeviceUIConfig_init_zero      {0, 0, 0, 0, 0, 0, _meshtastic_Theme_MIN, 0, 0, 0, _meshtastic_Language_MIN, false, meshtastic_NodeFilter_init_zero, false, meshtastic_NodeHighlight_init_zero}
#define meshtastic_NodeFilter_init_zero          {0, 0, 0, 0, 0, ""}
#define meshtastic_NodeHighlight_init_zero       {0, 0, 0, 0, ""}

/* Field tags (for use in manual encoding/decoding) */
#define meshtastic_NodeFilter_unknown_switch_tag 1
#define meshtastic_NodeFilter_offline_switch_tag 2
#define meshtastic_NodeFilter_public_key_switch_tag 3
#define meshtastic_NodeFilter_hops_away_tag      4
#define meshtastic_NodeFilter_position_switch_tag 5
#define meshtastic_NodeFilter_node_name_tag      6
#define meshtastic_NodeHighlight_chat_switch_tag 1
#define meshtastic_NodeHighlight_position_switch_tag 2
#define meshtastic_NodeHighlight_telemetry_switch_tag 3
#define meshtastic_NodeHighlight_iaq_switch_tag  4
#define meshtastic_NodeHighlight_node_name_tag   5
#define meshtastic_DeviceUIConfig_version_tag    1
#define meshtastic_DeviceUIConfig_screen_brightness_tag 2
#define meshtastic_DeviceUIConfig_screen_timeout_tag 3
#define meshtastic_DeviceUIConfig_screen_lock_tag 4
#define meshtastic_DeviceUIConfig_settings_lock_tag 5
#define meshtastic_DeviceUIConfig_pin_code_tag   6
#define meshtastic_DeviceUIConfig_theme_tag      7
#define meshtastic_DeviceUIConfig_alert_enabled_tag 8
#define meshtastic_DeviceUIConfig_banner_enabled_tag 9
#define meshtastic_DeviceUIConfig_ring_tone_id_tag 10
#define meshtastic_DeviceUIConfig_language_tag   11
#define meshtastic_DeviceUIConfig_node_filter_tag 12
#define meshtastic_DeviceUIConfig_node_highlight_tag 13

/* Struct field encoding specification for nanopb */
#define meshtastic_DeviceUIConfig_FIELDLIST(X, a) \
X(a, STATIC,   SINGULAR, UINT32,   version,           1) \
X(a, STATIC,   SINGULAR, UINT32,   screen_brightness,   2) \
X(a, STATIC,   SINGULAR, UINT32,   screen_timeout,    3) \
X(a, STATIC,   SINGULAR, BOOL,     screen_lock,       4) \
X(a, STATIC,   SINGULAR, BOOL,     settings_lock,     5) \
X(a, STATIC,   SINGULAR, UINT32,   pin_code,          6) \
X(a, STATIC,   SINGULAR, UENUM,    theme,             7) \
X(a, STATIC,   SINGULAR, BOOL,     alert_enabled,     8) \
X(a, STATIC,   SINGULAR, BOOL,     banner_enabled,    9) \
X(a, STATIC,   SINGULAR, UINT32,   ring_tone_id,     10) \
X(a, STATIC,   SINGULAR, UENUM,    language,         11) \
X(a, STATIC,   OPTIONAL, MESSAGE,  node_filter,      12) \
X(a, STATIC,   OPTIONAL, MESSAGE,  node_highlight,   13)
#define meshtastic_DeviceUIConfig_CALLBACK NULL
#define meshtastic_DeviceUIConfig_DEFAULT NULL
#define meshtastic_DeviceUIConfig_node_filter_MSGTYPE meshtastic_NodeFilter
#define meshtastic_DeviceUIConfig_node_highlight_MSGTYPE meshtastic_NodeHighlight

#define meshtastic_NodeFilter_FIELDLIST(X, a) \
X(a, STATIC,   SINGULAR, BOOL,     unknown_switch,    1) \
X(a, STATIC,   SINGULAR, BOOL,     offline_switch,    2) \
X(a, STATIC,   SINGULAR, BOOL,     public_key_switch,   3) \
X(a, STATIC,   SINGULAR, INT32,    hops_away,         4) \
X(a, STATIC,   SINGULAR, BOOL,     position_switch,   5) \
X(a, STATIC,   SINGULAR, STRING,   node_name,         6)
#define meshtastic_NodeFilter_CALLBACK NULL
#define meshtastic_NodeFilter_DEFAULT NULL

#define meshtastic_NodeHighlight_FIELDLIST(X, a) \
X(a, STATIC,   SINGULAR, BOOL,     chat_switch,       1) \
X(a, STATIC,   SINGULAR, BOOL,     position_switch,   2) \
X(a, STATIC,   SINGULAR, BOOL,     telemetry_switch,   3) \
X(a, STATIC,   SINGULAR, BOOL,     iaq_switch,        4) \
X(a, STATIC,   SINGULAR, STRING,   node_name,         5)
#define meshtastic_NodeHighlight_CALLBACK NULL
#define meshtastic_NodeHighlight_DEFAULT NULL

extern const pb_msgdesc_t meshtastic_DeviceUIConfig_msg;
extern const pb_msgdesc_t meshtastic_NodeFilter_msg;
extern const pb_msgdesc_t meshtastic_NodeHighlight_msg;

/* Defines for backwards compatibility with code written before nanopb-0.4.0 */
#define meshtastic_DeviceUIConfig_fields &meshtastic_DeviceUIConfig_msg
#define meshtastic_NodeFilter_fields &meshtastic_NodeFilter_msg
#define meshtastic_NodeHighlight_fields &meshtastic_NodeHighlight_msg

/* Maximum encoded size of messages (where known) */
#define MESHTASTIC_MESHTASTIC_DEVICE_UI_PB_H_MAX_SIZE meshtastic_DeviceUIConfig_size
#define meshtastic_DeviceUIConfig_size           99
#define meshtastic_NodeFilter_size               36
#define meshtastic_NodeHighlight_size            25

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
