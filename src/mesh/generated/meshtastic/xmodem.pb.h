/* Automatically generated nanopb header */
/* Generated by nanopb-0.4.9.1 */

#ifndef PB_MESHTASTIC_MESHTASTIC_XMODEM_PB_H_INCLUDED
#define PB_MESHTASTIC_MESHTASTIC_XMODEM_PB_H_INCLUDED
#include <pb.h>

#if PB_PROTO_HEADER_VERSION != 40
#error Regenerate this file with the current version of nanopb generator.
#endif

/* Enum definitions */
typedef enum _meshtastic_XModem_Control {
    meshtastic_XModem_Control_NUL = 0,
    meshtastic_XModem_Control_SOH = 1,
    meshtastic_XModem_Control_STX = 2,
    meshtastic_XModem_Control_EOT = 4,
    meshtastic_XModem_Control_ACK = 6,
    meshtastic_XModem_Control_NAK = 21,
    meshtastic_XModem_Control_CAN = 24,
    meshtastic_XModem_Control_CTRLZ = 26
} meshtastic_XModem_Control;

/* Struct definitions */
typedef PB_BYTES_ARRAY_T(128) meshtastic_XModem_buffer_t;
typedef struct _meshtastic_XModem {
    meshtastic_XModem_Control control;
    uint16_t seq;
    uint16_t crc16;
    meshtastic_XModem_buffer_t buffer;
} meshtastic_XModem;


#ifdef __cplusplus
extern "C" {
#endif

/* Helper constants for enums */
#define _meshtastic_XModem_Control_MIN meshtastic_XModem_Control_NUL
#define _meshtastic_XModem_Control_MAX meshtastic_XModem_Control_CTRLZ
#define _meshtastic_XModem_Control_ARRAYSIZE ((meshtastic_XModem_Control)(meshtastic_XModem_Control_CTRLZ+1))

#define meshtastic_XModem_control_ENUMTYPE meshtastic_XModem_Control


/* Initializer values for message structs */
#define meshtastic_XModem_init_default           {_meshtastic_XModem_Control_MIN, 0, 0, {0, {0}}}
#define meshtastic_XModem_init_zero              {_meshtastic_XModem_Control_MIN, 0, 0, {0, {0}}}

/* Field tags (for use in manual encoding/decoding) */
#define meshtastic_XModem_control_tag            1
#define meshtastic_XModem_seq_tag                2
#define meshtastic_XModem_crc16_tag              3
#define meshtastic_XModem_buffer_tag             4

/* Struct field encoding specification for nanopb */
#define meshtastic_XModem_FIELDLIST(X, a) \
X(a, STATIC,   SINGULAR, UENUM,    control,           1) \
X(a, STATIC,   SINGULAR, UINT32,   seq,               2) \
X(a, STATIC,   SINGULAR, UINT32,   crc16,             3) \
X(a, STATIC,   SINGULAR, BYTES,    buffer,            4)
#define meshtastic_XModem_CALLBACK NULL
#define meshtastic_XModem_DEFAULT NULL

extern const pb_msgdesc_t meshtastic_XModem_msg;

/* Defines for backwards compatibility with code written before nanopb-0.4.0 */
#define meshtastic_XModem_fields &meshtastic_XModem_msg

/* Maximum encoded size of messages (where known) */
#define MESHTASTIC_MESHTASTIC_XMODEM_PB_H_MAX_SIZE meshtastic_XModem_size
#define meshtastic_XModem_size                   141

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
