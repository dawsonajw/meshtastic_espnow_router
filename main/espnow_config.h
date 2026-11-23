/* ESPNOW Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#ifndef ESPNOW_CONFIG_H
#define ESPNOW_CONFIG_H

#define ESPNOW_WIFI_MODE WIFI_MODE_STA
#define ESPNOW_WIFI_IF   WIFI_IF_STA
#define ESPNOW_CHANNEL   1
#define ESPNOW_SEND_DELAY 1000  // ms between sends
#define CONFIG_ESPNOW_ENABLE_LONG_RANGE 1

// Data type definitions
#define ESPNOW_DATA_BROADCAST 0
#define ESPNOW_DATA_UNICAST   1

#define CONFIG_ESPNOW_PMK "pmk4779874676932"
#define CONFIG_ESPNOW_LMK "lmk8686458277271"

/* ESPNOW packet structure with CRC and sequence number */
typedef struct {
    uint8_t type;           // Broadcast or unicast
    uint8_t state;          // State indicator
    uint16_t seq_num;       // Sequence number
    uint16_t crc;           // CRC16 value
    uint32_t magic;         // Magic number for device identification
    uint8_t from_router;      // Distinguish router messages from device message 
    uint8_t payload[0];     // Flexible array for actual data
} __attribute__((packed)) espnow_data_t;

// Maximum payload size (250 bytes for V1.0 | 1470 bytes for V2.0 - header overhead)
#define ESPNOW_MAX_PAYLOAD ESP_NOW_MAX_IE_DATA_LEN - sizeof(espnow_data_t) 

/* Parameters for sending ESPNOW data */
typedef struct {
    bool unicast;           // Send unicast or broadcast
    uint8_t state;
    uint32_t magic;
    uint16_t count;
    uint16_t delay;
    int len;
    uint8_t *buffer;
    uint8_t dest_mac[ESP_NOW_ETH_ALEN];
} espnow_send_param_t;

/* Structure to pass data and length through queue */
typedef struct {
    int len;
    uint8_t recv_mac[ESP_NOW_ETH_ALEN];
    uint8_t data[];
} recv_packet_t;

#endif