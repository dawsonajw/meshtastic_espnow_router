
/*
 * protocol.h - Meshtastic <-> ESP-NOW router message protocol
 *
 * Shared by the router and every device firmware. Both ends must use the
 * same version of this file. Never renumber existing enum values; add new
 * ones at the end.
 *
 * Every message below is carried inside espnow_data_t.payload. The first
 * byte of every message is msg_type, so the receiver reads it first to
 * know which struct the rest of the payload is.
 *
 * Device identity
 *   A device is identified by its Wi-Fi STA MAC address (the ESP-NOW
 *   sender address, recv_info->src_addr). No ID is carried in the payload.
 *   Mesh users refer to a device by the last 4 hex digits of its MAC
 *   (e.g. "7408"); a longer suffix resolves any ambiguity.
 *
 * Commands allowed per endpoint type
 *   EP_BINARY_SENSOR    GET
 *   EP_ANALOG_SENSOR    GET
 *   EP_BINARY_ACTUATOR  GET, SET
 *   EP_LEVEL_ACTUATOR   GET, SET
 *   The router checks this before sending; the device checks it again and
 *   replies ok = 0 to anything it can't handle.
 *
 * SET semantics
 *   has_arg = 0          toggle (binary: 0 <-> 1, level: 0 <-> last non-zero)
 *   has_arg = 1, binary  value must be 0 or 1
 *   has_arg = 1, level   value is the new level
 *
 * Every GET and SET is answered with a report_msg_t holding the endpoint's
 * actual value afterwards. Sensors also send report_msg_t on change.
 *
 * Values and units
 *   report_msg_t.value and cmd_msg_t.value are integers in engineering
 *   units, never raw ADC counts. The device converts and calibrates.
 *   Each endpoint declares its unit and decimal scale once, in its
 *   endpoint_desc_t:  real value = value * 10^scale
 *     scale = -1, unit = UNIT_CELSIUS, value = 235  ->  23.5 C
 *     scale =  3, unit = UNIT_PASCAL,  value = 101  ->  101 kPa
 *     scale =  0, unit = UNIT_PERCENT, value = 45   ->  45 %
 *   Binary endpoints use UNIT_NONE and scale 0 (value is 0 or 1).
 *
 * Mesh text grammar (parsed by the router)
 *   <mac4>.<endpoint>            same as get
 *   <mac4>.<endpoint> get
 *   <mac4>.<endpoint> set        toggle
 *   <mac4>.<endpoint> set <n>
 *   list                         known devices and endpoints
 *
 * Deliberately left out for now (planned, not forgotten)
 *   retries and request/response matching, heartbeats and expiry,
 *   min/max, deadbands, multi-endpoint reports,
 *   friendly names, NVS persistence, protocol version byte
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

/* ------------------------------------------------------------------------
 * IDs (stored on the wire as uint8_t; 0 always means invalid/unset)
 * --------------------------------------------------------------------- */

typedef enum { 
    MSG_INVALID = 0, 
    MSG_ANNOUNCE = 1, 
    MSG_CMD = 2, 
    MSG_REPORT = 3
} msg_type_t; // Message Types

typedef enum { 
    CMD_INVALID = 0, 
    CMD_GET = 1, 
    CMD_SET = 2
} cmd_id_t; // Command Types

typedef enum{
    EP_INVALID = 0,
    EP_BINARY_SENSOR = 1,
    EP_ANALOG_SENSOR = 2,
    EP_BINARY_ACTUATOR = 3,
    EP_LEVEL_ACTUATOR = 4,
    EP_TYPE_COUNT,
} ep_type_t; //Endpoint Types

/* Units for endpoint values. Append new units at the end with the next
 * number; never renumber or reuse a value once devices are deployed.
 * Prefer the base unit and use scale for decimals (e.g. 23.5 C is
 * UNIT_CELSIUS with scale -1), rather than adding milli/kilo variants. */
typedef enum {
    UNIT_NONE        = 0,  /* binary endpoints, unitless counts */
    UNIT_PERCENT     = 1,  /* %  (level, duty cycle, battery) */
    UNIT_CELSIUS     = 2,  /* degrees C */
    UNIT_FAHRENHEIT  = 3,  /* degrees F */
    UNIT_RH          = 4,  /* % relative humidity */
    UNIT_VOLT        = 5,  /* V */
    UNIT_AMP         = 6,  /* A */
    UNIT_WATT        = 7,  /* W */
    UNIT_PASCAL      = 8,  /* Pa (use scale 3 for kPa) */
    UNIT_LUX         = 9,  /* lx */
    UNIT_METER       = 10, /* m  (distance, water level; scale -3 for mm) */
    UNIT_LITER       = 11, /* L */
    UNIT_LITER_MIN   = 12, /* L/min (flow) */
    UNIT_PPM         = 13, /* ppm (CO2, gas) */
    UNIT_SECOND      = 14, /* s */
    UNIT_DEGREE      = 15, /* angle, e.g. servo position */
    UNIT_HERTZ       = 16, /* Hz */
    UNIT_COUNT,             /* number of units; keep last, not sent */
} unit_t;

/* ------------------------------------------------------------------------
 * Sizes
 * --------------------------------------------------------------------- */

#define EP_NAME_LEN        10   /* including the terminating '\0' */

/* espnow_data_t header is 11 bytes, so 250 - 11 = 239 bytes of payload.
 * announce_msg_t: 2-byte header + 12 bytes per endpoint -> 19 fit;
 * capped lower to leave room for later additions. */
#define ANNOUNCE_MAX_EPS   10

/* ------------------------------------------------------------------------
 * Messages (wire format: packed, fixed layout)
 * --------------------------------------------------------------------- */

 /* One controllable or readable thing on a device. Part of announce_msg_t. */
typedef struct {
    uint8_t ep_id;
    uint8_t ep_type;            /* ep_type_t */
    uint8_t unit;
    int8_t   scale;             /* value * 10^scale, e.g. -1 → 235 means 23.5 */             
    char    name[EP_NAME_LEN];  /* e.g. "led", null-terminated */
} __attribute__((packed)) endpoint_desc_t;

/* Device -> router, sent at boot. Lists the device's endpoints. */
typedef struct {
    uint8_t         msg_type;   /* MSG_ANNOUNCE */
    uint8_t         ep_count;   /* <= ANNOUNCE_MAX_EPS */
    endpoint_desc_t eps[];
} __attribute__((packed)) announce_msg_t;

/* Router -> device. */
typedef struct {
    uint8_t msg_type;           /* MSG_CMD */
    uint8_t ep_id;
    uint8_t cmd;                /* cmd_type_t */
    uint8_t has_arg;            /* SET only: 0 = toggle, 1 = use value */
    int32_t value;
} __attribute((packed)) cmd_msg_t;

/* Device -> router. Answer to GET/SET, or unsolicited sensor change. */
typedef struct {
    uint8_t msg_type;           /* MSG_REPORT */
    uint8_t ep_id;
    uint8_t ok;                 /* 1 = ok, 0 = error */
    int32_t value;              /* actual value after the command */
} __attribute((packed)) report_msg_t;


/* ------------------------------------------------------------------------
 * Wire size checks: if one fires, the wire format changed. Update the
 * number deliberately and reflash both router and devices.
 * --------------------------------------------------------------------- */

_Static_assert(sizeof(endpoint_desc_t) == 14, "endpoint_desc_t wire size changed");
_Static_assert(sizeof(announce_msg_t)  == 2,  "announce_msg_t header size changed");
_Static_assert(sizeof(cmd_msg_t)       == 8,  "cmd_msg_t wire size changed");
_Static_assert(sizeof(report_msg_t)    == 7,  "report_msg_t wire size changed");
_Static_assert(sizeof(announce_msg_t) + ANNOUNCE_MAX_EPS * sizeof(endpoint_desc_t) <= 239,
               "announce with ANNOUNCE_MAX_EPS endpoints exceeds ESP-NOW v1 payload");

#endif /* PROTOCOL_H */