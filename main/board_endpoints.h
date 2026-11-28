#ifndef BOARD_ENDPOINTS_H
#define BOARD_ENDPOINTS_H

#include <driver/gpio.h>
#include <esp_err.h>
#include "protocol.h"
#include "espnow_router_config.h"

 /* One controllable or readable thing on a device with its specified pin  */
typedef struct {
    gpio_num_t pin;
    endpoint_desc_t ep_desc;
} ep_local_config_t;

esp_err_t board_init(void);
esp_err_t board_announce(announce_msg_t *ann, size_t buf_size, size_t *out_len);
esp_err_t board_get_ep_by_id(uint8_t ep_id, ep_local_config_t *out);
uint8_t   board_ep_count(void);

#endif