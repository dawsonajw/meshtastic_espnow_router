#ifndef DEVICE_REGISTRY_H
#define DEVICE_REGISTRY_H

#include <stdint.h>
#include <esp_now.h>
#include "protocol.h"

/* Using encrypted unicasts, device limit is set by maximum number of
 * encrypted devices */
#define REGISTRY_MAX_DEVICES CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM

typedef struct {
    bool     in_use;
    uint8_t  mac[ESP_NOW_ETH_ALEN];
    uint8_t  ep_count;
    endpoint_desc_t eps[ANNOUNCE_MAX_EPS];
    int8_t   rssi;
    int64_t  last_seen_us;
} device_entry_t;

esp_err_t registry_init(void);
esp_err_t registry_update(const uint8_t mac[ESP_NOW_ETH_ALEN], const announce_msg_t *ann_msg, int8_t rssi);
esp_err_t registry_find_by_mac(const uint8_t mac[ESP_NOW_ETH_ALEN], device_entry_t *out);
esp_err_t registry_find_by_suffix(const char *hex, device_entry_t *out);
esp_err_t registry_next(int *registry_cursor, device_entry_t *out);
esp_err_t registry_log(void);

#endif
