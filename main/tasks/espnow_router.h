#ifndef ESPNOW_ROUTER_H
#define ESPNOW_ROUTER_H

#include <esp_wifi.h>
#include <esp_mac.h>
#include <esp_now.h>
#include <esp_crc.h>

#include "espnow_config.h"

void wifi_init(void);
void espnow_send_cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status);
int espnow_data_parse(uint8_t *data, uint16_t data_len, 
                            uint8_t *state, uint16_t *seq, uint32_t *magic, uint8_t *from_router);
void espnow_recv_cb(const esp_now_recv_info_t *recv_info, 
                          const uint8_t *data, int len);
void espnow_data_prepare(espnow_send_param_t *send_param);
void espnow_recv_task_router(void *pvParameter);
void espnow_recv_task_device(void *pvParameter);
void espnow_send_task_router(void *pvParameter);
void espnow_send_task_device(void *pvParameter);
void espnow_deinit(espnow_send_param_t *send_param);
esp_err_t espnow_init(void);
                                           
#endif