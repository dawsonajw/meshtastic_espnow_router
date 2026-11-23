#include <string.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <esp_now.h>
#include <esp_crc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "esp_random.h"
#include "tasks/espnow_router.h"
#include "espnow_config.h"
#include "espnow_router_config.h"

extern QueueHandle_t mesh_send_queue;
extern QueueHandle_t espnow_send_queue;
extern QueueHandle_t espnow_recv_queue;
extern QueueHandle_t display_queue;
extern QueueHandle_t action_queue;

/* WiFi should start before using ESPNOW */
void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(ESPNOW_WIFI_MODE));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));

#if CONFIG_ESPNOW_ENABLE_LONG_RANGE
    ESP_ERROR_CHECK(esp_wifi_set_protocol(ESPNOW_WIFI_IF, 
        WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N|WIFI_PROTOCOL_LR));
#endif
}

/* ESPNOW sending callback function */
void espnow_send_cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    if (tx_info == NULL) {
        ESP_LOGE("espnow_send_cb", "Send cb arg error");
        return;
    }

    if (status == ESP_NOW_SEND_SUCCESS) {
        ESP_LOGI("espnow_send_cb", "Send success to " MACSTR, MAC2STR(tx_info->des_addr));
    } else {
        ESP_LOGW("espnow_send_cb", "Send failed to " MACSTR, MAC2STR(tx_info->des_addr));
    }
}

/* Parse ESPNOW data and extract state, sequence, magic number */
int espnow_data_parse(uint8_t *data, uint16_t data_len, 
                            uint8_t *state, uint16_t *seq, uint32_t *magic, uint8_t *from_router)
{
    espnow_data_t *buf = (espnow_data_t *)data;
    uint16_t crc, crc_cal = 0;

    if (data_len < sizeof(espnow_data_t)) {
        ESP_LOGE("espnow_data_parse", "Receive ESPNOW data too short, len:%d", data_len);
        return -1;
    }

    *state = buf->state;
    *seq = buf->seq_num;
    *magic = buf->magic;
    *from_router = buf->from_router;
    
    // Save the received CRC
    crc = buf->crc;
    
    // Zero out CRC field before calculating
    buf->crc = 0;
    
    // Calculate CRC over entire packet with CRC field set to 0
    crc_cal = esp_crc16_le(UINT16_MAX, (uint8_t const *)buf, data_len);

    if (crc_cal == crc) {
        buf->crc = crc;  // Restore CRC
        return buf->type;
    }

    ESP_LOGW("espnow_data_parse", "CRC mismatch! Calculated: 0x%04x, Received: 0x%04x, len: %d", 
             crc_cal, crc, data_len);
    buf->crc = crc;  // Restore CRC even on failure
    return -1;
}

/* ESPNOW receiving callback function */
void espnow_recv_cb(const esp_now_recv_info_t *recv_info, 
                          const uint8_t *data, int len)
{
    
    // Allocate space for data + length info
    recv_packet_t *recv_packet = malloc(sizeof(recv_packet_t) + len);

    if (recv_packet == NULL) {
        ESP_LOGE("espnow_recv_cb", "Malloc receive data fail");
        return;
    }

    recv_packet->len = len;
    memcpy(recv_packet->recv_mac, recv_info->src_addr, ESP_NOW_ETH_ALEN);
    memcpy(recv_packet->data, data, len);
    // ESP_LOGI("expnow_rcv_cb", "Receive data from: "MACSTR"", MAC2STR(recv_packet->recv_mac));
    // Send POINTER to receive queue for processing
    if (xQueueSend(espnow_recv_queue, &recv_packet, portMAX_DELAY) != pdTRUE) {
        ESP_LOGW("espnow_recv_cb", "Send receive queue fail");
        free(recv_packet);
    }
}

/* Prepare ESPNOW data to be sent */
void espnow_data_prepare(espnow_send_param_t *send_param)
{
    espnow_data_t *buf = (espnow_data_t *)send_param->buffer;

    assert(send_param->len >= sizeof(espnow_data_t));

    buf->type = send_param->unicast ? ESPNOW_DATA_UNICAST : ESPNOW_DATA_BROADCAST;
    buf->state = send_param->state;
    buf->seq_num = send_param->count;
    buf->magic = send_param->magic;
    buf->from_router = IS_ROUTER;
    buf->crc = 0;
    buf->crc = esp_crc16_le(UINT16_MAX, (uint8_t const *)buf, send_param->len);
}

/* Router task to receive ESPNOW data and forward to your existing queues */
void espnow_recv_task_router(void *pvParameter)
{
    recv_packet_t *recv_packet = NULL;
    
    while (1) {
        // Receive POINTER from queue
        if (xQueueReceive(espnow_recv_queue, &recv_packet, portMAX_DELAY) == pdTRUE) {
            uint8_t state;
            uint16_t seq;
            uint32_t magic;
            uint8_t from_router;
            
            espnow_data_t *buf = (espnow_data_t *)recv_packet->data;
            
            // Use actual received length for CRC calculation
            int ret = espnow_data_parse(recv_packet->data, recv_packet->len, &state, &seq, &magic, &from_router);
            
            if (ret == ESPNOW_DATA_BROADCAST) {
                ESP_LOGI("espnow_recv_task_router", "Receive %dth broadcast data from: "MACSTR", len: %d, Router: %d", seq, MAC2STR(recv_packet->recv_mac), recv_packet->len, from_router);
                               
                /* If this is a Router and the broadcast message is from Device,
                add Device to Router's peer list */
                if(!from_router){
                    /* If MAC address does not exist in peer list, add it to peer list. */
                    if (esp_now_is_peer_exist(recv_packet->recv_mac) == false) {
                        esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
                        if (peer == NULL) {
                            ESP_LOGE("espnow_recv_task_router", "Malloc peer information fail");
                            break;
                        }
                        memset(peer, 0, sizeof(esp_now_peer_info_t));
                        peer->channel = ESPNOW_CHANNEL;
                        peer->ifidx = ESPNOW_WIFI_IF;
                        peer->encrypt = true;
                        memcpy(peer->lmk, CONFIG_ESPNOW_LMK, ESP_NOW_KEY_LEN);
                        memcpy(peer->peer_addr, recv_packet->recv_mac, ESP_NOW_ETH_ALEN);
                        ESP_ERROR_CHECK( esp_now_add_peer(peer) );
                        free(peer);
                        ESP_LOGI("espnow_recv_task_router", "Added "MACSTR" to Router peer list.", MAC2STR(recv_packet->recv_mac));

                        uint8_t msg_data[1];
                        memset(msg_data, 0, 1);
                        // Ensure null termination
                        msg_data[0] = '\0';

                        ESP_LOGI("espnow_recv_task_device", "Echo Back Empty Message.");

                        if (xQueueSend(espnow_send_queue, msg_data, 100 / portTICK_PERIOD_MS) != pdTRUE) {
                            ESP_LOGW("espnow_recv_task_device", "Send to espnow send queue fail");
                        }
                    }
                }                
            } else if (ret == ESPNOW_DATA_UNICAST) {
                ESP_LOGI("espnow_recv_task_router", "Receive %dth unicast data from: "MACSTR", len: %d, Router: %d", seq, MAC2STR(recv_packet->recv_mac), recv_packet->len, from_router);

                // Extract the actual message from payload
                int payload_len = recv_packet->len - sizeof(espnow_data_t);
                if (payload_len > 0 && payload_len < BUF_SIZE) {
                    uint8_t msg_data[BUF_SIZE];
                    memset(msg_data, 0, BUF_SIZE);
                    memcpy(msg_data, buf->payload, payload_len);
                    
                    // Ensure null termination
                    msg_data[payload_len] = '\0';
                    
                    ESP_LOGI("espnow_recv_task_router", "Message: %s", (char*)msg_data);
                    
                    // If Router receives unicast from Device simply forward to Meshtastic
                    if(IS_ROUTER){
                        if(!from_router){
                            if (xQueueSend(mesh_send_queue, msg_data, 100 / portTICK_PERIOD_MS) != pdTRUE) {
                                ESP_LOGW("espnow_recv_task_router", "Send to mesh send queue fail");
                            }
                        }
                    } 
                }
            } else {
                ESP_LOGI("espnow_recv_task_router", "Receive error data or CRC mismatch");
            }
            
            // Free the malloc'd packet
            free(recv_packet);
            recv_packet = NULL;
        }
    }
}

/* Device task to receive ESPNOW data and forward to your existing queues */
void espnow_recv_task_device(void *pvParameter)
{
    recv_packet_t *recv_packet = NULL;
    
    while (1) {
        // Receive POINTER from queue
        if (xQueueReceive(espnow_recv_queue, &recv_packet, portMAX_DELAY) == pdTRUE) {
            uint8_t state;
            uint16_t seq;
            uint32_t magic;
            uint8_t from_router;
            
            espnow_data_t *buf = (espnow_data_t *)recv_packet->data;
            
            // Use actual received length for CRC calculation
            int ret = espnow_data_parse(recv_packet->data, recv_packet->len, &state, &seq, &magic, &from_router);
            
            if (ret == ESPNOW_DATA_BROADCAST) {
                ESP_LOGI("espnow_recv_task_device", "Receive %dth broadcast data from: "MACSTR", len: %d, Router: %d", seq, MAC2STR(recv_packet->recv_mac), recv_packet->len, from_router);
                
                // Handle broadcast data - peer discovery, etc.
                // You can add peer to list here if needed
                
                /* If this is a Device and the broadcast message is from Router
                add Router to Device's peer list */
                if(from_router){
                    /* If MAC address does not exist in peer list, add it to peer list. 
                    if (esp_now_is_peer_exist(recv_packet->recv_mac) == false) {
                        esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
                        if (peer == NULL) {
                            ESP_LOGE("espnow_recv_task_device", "Malloc peer information fail");
                            break;
                        }
                        memset(peer, 0, sizeof(esp_now_peer_info_t));
                        peer->channel = ESPNOW_CHANNEL;
                        peer->ifidx = ESPNOW_WIFI_IF;
                        peer->encrypt = true;
                        memcpy(peer->lmk, CONFIG_ESPNOW_LMK, ESP_NOW_KEY_LEN);
                        memcpy(peer->peer_addr, recv_packet->recv_mac, ESP_NOW_ETH_ALEN);
                        ESP_ERROR_CHECK( esp_now_add_peer(peer) );
                        free(peer);

                        ESP_LOGI("espnow_recv_task_device", "Added "MACSTR" to Device peer list.", MAC2STR(recv_packet->recv_mac));
                    }*/
                    
                    // Extract the actual message from payload; if has length display, else send empty boradcast
                    int payload_len = recv_packet->len - sizeof(espnow_data_t);
                    if (payload_len > 0 && payload_len < BUF_SIZE) {
                        uint8_t msg_data[BUF_SIZE];
                        memset(msg_data, 0, BUF_SIZE);
                        memcpy(msg_data, buf->payload, payload_len);
                        
                        // Ensure null termination
                        msg_data[payload_len] = '\0';
                        
                        ESP_LOGI("espnow_recv_task_device", "Message: %s", (char*)msg_data);

                        // Forward to your existing queues
                        if (xQueueSend(display_queue, msg_data, 100 / portTICK_PERIOD_MS) != pdTRUE) {
                            ESP_LOGW("espnow_recv_task_device", "Send to display queue fail");
                        }
                    }
                    else{
                        uint8_t msg_data[1];
                        memset(msg_data, 0, 1);
                        // Ensure null termination
                        msg_data[0] = '\0';

                        ESP_LOGI("espnow_recv_task_device", "Echo Back Empty Message.");

                        if (xQueueSend(espnow_send_queue, msg_data, 100 / portTICK_PERIOD_MS) != pdTRUE) {
                            ESP_LOGW("espnow_recv_task_device", "Send to espnow send queue fail");
                        }
                    }
                }
                
            } else if (ret == ESPNOW_DATA_UNICAST) {
                ESP_LOGI("espnow_recv_task_device", "Receive %dth unicast data from: "MACSTR", len: %d, Router: %d", seq, MAC2STR(recv_packet->recv_mac), recv_packet->len, from_router);
                
                // Extract the actual message from payload
                int payload_len = recv_packet->len - sizeof(espnow_data_t);
                if (payload_len > 0 && payload_len < BUF_SIZE) {
                    uint8_t msg_data[BUF_SIZE];
                    memset(msg_data, 0, BUF_SIZE);
                    memcpy(msg_data, buf->payload, payload_len);
                    
                    // Ensure null termination
                    msg_data[payload_len] = '\0';
                    
                    ESP_LOGI("espnow_recv_task_device", "Message: %s", (char*)msg_data);
                    
                    if(from_router){
                        // Forward to your existing queues
                        if (xQueueSend(action_queue, msg_data, 100 / portTICK_PERIOD_MS) != pdTRUE) {
                            ESP_LOGW("espnow_recv_task_device", "Send to action queue fail");
                        }
                        if (xQueueSend(display_queue, msg_data, 100 / portTICK_PERIOD_MS) != pdTRUE) {
                            ESP_LOGW("espnow_recv_task_device", "Send to display queue fail");
                        }
                    }    
                }
            } else {
                ESP_LOGI("espnow_recv_task_device", "Receive error data or CRC mismatch");
            }
            
            // Free the malloc'd packet
            free(recv_packet);
            recv_packet = NULL;
        }
    }
}

/* Router task to send ESPNOW data from queue */
void espnow_send_task_router(void *pvParameter)
{
    espnow_send_param_t *send_param = (espnow_send_param_t *)pvParameter;
    uint8_t user_msg[BUF_SIZE];
    
    // After turning on, broadcast empty data once
    if (esp_now_send(send_param->dest_mac, send_param->buffer, send_param->len) != ESP_OK) {
        ESP_LOGE("espnow_send_task_router", "Send error");
        vTaskDelete(NULL);
    }
    /*
    // After turning on, broadcast empty data for every 6 seconds for 1 minute
    for(int i = 0; i < 10; i++){
        if (esp_now_send(send_param->dest_mac, send_param->buffer, send_param->len) != ESP_OK) {
            ESP_LOGE(TAG, "Send error");
            vTaskDelete(NULL);
        }
        vTaskDelay(6000 / portTICK_PERIOD_MS);
    }
    */
    
    while (1) {
        // Wait for messages from your existing espnow_send_queue
        if (xQueueReceive(espnow_send_queue, &user_msg, portMAX_DELAY) == pdTRUE) {
            
            int msg_len = strlen((char*)user_msg);
            if (msg_len > ESPNOW_MAX_PAYLOAD) {
                msg_len = ESPNOW_MAX_PAYLOAD;
            }
            
            // Prepare buffer with header + payload
            int total_len = sizeof(espnow_data_t) + msg_len;
            uint8_t *send_buf = malloc(total_len);
            
            if (send_buf == NULL) {
                ESP_LOGE("espnow_send_task_router", "Malloc send buffer fail");
                continue;
            }
            
            // Copy user message to payload
            memcpy(((espnow_data_t *)send_buf)->payload, user_msg, msg_len);
            
            // Update send parameters
            send_param->len = total_len;
            send_param->buffer = send_buf;
            send_param->count++;
            // If message is empty broadcast
            if (msg_len == 0){
                send_param->unicast = false;
                memcpy(send_param->dest_mac, "\xFF\xFF\xFF\xFF\xFF\xFF", ESP_NOW_ETH_ALEN);
                
                // Prepare data with CRC
                espnow_data_prepare(send_param);
        
                if (esp_now_send(send_param->dest_mac, send_param->buffer, send_param->len) != ESP_OK) {
                    ESP_LOGE("espnow_send_task_router", "Send error");
                } else {
                    ESP_LOGI("espnow_send_task_router", "Sent broadcast to "MACSTR" message %d: %s", MAC2STR(send_param->dest_mac), send_param->count, (char*)user_msg);
                }
                vTaskDelay(send_param->delay / portTICK_PERIOD_MS);           
            }
            else{//Else send message as unicast to each Device in peer list
                esp_now_peer_info_t peer;
                esp_now_peer_num_t peer_num;

                esp_now_get_peer_num(&peer_num);

                send_param->unicast = true;

                for(int i = 0; i < peer_num.total_num; i++){
                    bool from_head = (i == 0);

                    if (esp_now_fetch_peer(from_head, &peer) == ESP_OK){
                        memcpy(send_param->dest_mac, peer.peer_addr, ESP_NOW_ETH_ALEN);
                        
                        // Prepare data with CRC
                        espnow_data_prepare(send_param);
                        if (esp_now_send(send_param->dest_mac, send_param->buffer, send_param->len) != ESP_OK) {
                            ESP_LOGE("espnow_send_task_router", "Send error");
                        } else {
                            ESP_LOGI("espnow_send_task_router", "Sent unicast to "MACSTR" message %d: %s", MAC2STR(send_param->dest_mac), send_param->count, (char*)user_msg);
                        }           
                    }
                    vTaskDelay(send_param->delay / portTICK_PERIOD_MS);
                }
            }
            free(send_buf);
        }
    }
}

/* Device task to send ESPNOW data from queue */
void espnow_send_task_device(void *pvParameter)
{
    espnow_send_param_t *send_param = (espnow_send_param_t *)pvParameter;
    uint8_t user_msg[BUF_SIZE];
    
    // After turning on, broadcast empty data once
    if (esp_now_send(send_param->dest_mac, send_param->buffer, send_param->len) != ESP_OK) {
        ESP_LOGE("espnow_send_task_device", "Send error");
        vTaskDelete(NULL);
    }
    
    while (1) {
        // Wait for messages from your existing espnow_send_queue
        if (xQueueReceive(espnow_send_queue, &user_msg, portMAX_DELAY) == pdTRUE) {
            
            int msg_len = strlen((char*)user_msg);
            if (msg_len > ESPNOW_MAX_PAYLOAD) {
                msg_len = ESPNOW_MAX_PAYLOAD;
            }
            
            // Prepare buffer with header + payload
            int total_len = sizeof(espnow_data_t) + msg_len;
            uint8_t *send_buf = malloc(total_len);
            
            if (send_buf == NULL) {
                ESP_LOGE("espnow_send_task_device", "Malloc send buffer fail");
                continue;
            }
            
            // Copy user message to payload
            memcpy(((espnow_data_t *)send_buf)->payload, user_msg, msg_len);
            
            // Update send parameters
            send_param->len = total_len;
            send_param->buffer = send_buf;
            send_param->count++;

            // If message is empty broadcast
            if (msg_len == 0){
                send_param->unicast = false;
                memcpy(send_param->dest_mac, "\xFF\xFF\xFF\xFF\xFF\xFF", ESP_NOW_ETH_ALEN);
                // Prepare data with CRC
                espnow_data_prepare(send_param);
        
                if (esp_now_send(send_param->dest_mac, send_param->buffer, send_param->len) != ESP_OK) {
                    ESP_LOGE("espnow_send_task_device", "Send error");
                } else {
                    ESP_LOGI("espnow_send_task_device", "Sent broadcast to "MACSTR" message %d: %s", MAC2STR(send_param->dest_mac), send_param->count, (char*)user_msg);
                }
                vTaskDelay(send_param->delay / portTICK_PERIOD_MS);           
            }
            else{
                esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
                
                if (esp_now_fetch_peer(true, peer) != ESP_OK) {
                    ESP_LOGE("espnow_send_task_device", "Peer list contains no address");
                }
                else{
                    send_param->unicast = true;
                    memcpy(send_param->dest_mac, peer->peer_addr, ESP_NOW_ETH_ALEN);
                }
                
                // Prepare data with CRC
                espnow_data_prepare(send_param);

                // Send
                if (esp_now_send(send_param->dest_mac, send_param->buffer, send_param->len) != ESP_OK) {
                    ESP_LOGE("espnow_send_task_device", "Send error");
                } else {
                    ESP_LOGI("espnow_send_task_device", "Sent unicast to "MACSTR" message %d: %s", MAC2STR(send_param->dest_mac), send_param->count, (char*)user_msg);
                }
                vTaskDelay(send_param->delay / portTICK_PERIOD_MS); 
            }
            free(send_buf);  
        }
    }
}

/* Deinitialize ESPNOW */
void espnow_deinit(espnow_send_param_t *send_param)
{
    free(send_param->buffer);
    free(send_param);
    vSemaphoreDelete(espnow_send_queue);
    vSemaphoreDelete(espnow_recv_queue);
    esp_now_deinit();
}

/* Initialize ESPNOW */
esp_err_t espnow_init(void)
{
    espnow_send_param_t *send_param;

    espnow_send_queue = xQueueCreate(10, BUF_SIZE);
    if (espnow_send_queue == NULL) {
        ESP_LOGE("espnow_init", "Create send queue fail");
        return ESP_FAIL;
    }

    // Queue stores pointers to recv_packet_t structures
    espnow_recv_queue = xQueueCreate(10, sizeof(void *));
    if (espnow_recv_queue == NULL) {
        ESP_LOGE("espnow_init", "Create receive queue fail");
        vSemaphoreDelete(espnow_send_queue);
        return ESP_FAIL;
    }

    /* Initialize ESPNOW and register sending and receiving callback function. */
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));

    /* Set primary master key. */
    ESP_ERROR_CHECK(esp_now_set_pmk((uint8_t *)CONFIG_ESPNOW_PMK));

    /* Add broadcast peer information to peer list. */
    esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
    if (peer == NULL) {
        ESP_LOGE("espnow_init", "Malloc peer information fail");
        vSemaphoreDelete(espnow_send_queue);
        vSemaphoreDelete(espnow_recv_queue);
        esp_now_deinit();
        return ESP_FAIL;
    }
    memset(peer, 0, sizeof(esp_now_peer_info_t));
    peer->channel = ESPNOW_CHANNEL;
    peer->ifidx = ESPNOW_WIFI_IF;
    peer->encrypt = false;
    memcpy(peer->peer_addr, BROADCAST_MAC, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK(esp_now_add_peer(peer));
    
    // If Device, add Router to peer list
    if(!IS_ROUTER){
        peer->encrypt = true;
        memcpy(peer->lmk, CONFIG_ESPNOW_LMK, ESP_NOW_KEY_LEN);
        memcpy(peer->peer_addr, ROUTER_MAC, ESP_NOW_ETH_ALEN);
        ESP_ERROR_CHECK(esp_now_add_peer(peer));
    }
    free(peer);

    /* Initialize sending parameters. */
    send_param = malloc(sizeof(espnow_send_param_t));
    memset(send_param, 0, sizeof(espnow_send_param_t));
    if (send_param == NULL) {
        ESP_LOGE("espnow_init", "Malloc send parameter fail");
        vSemaphoreDelete(espnow_send_queue);
        vSemaphoreDelete(espnow_recv_queue);
        esp_now_deinit();
        return ESP_FAIL;
    }
    send_param->unicast = false;
    send_param->state = 0;
    send_param->magic = esp_random();
    send_param->count = 0;
    send_param->delay = ESPNOW_SEND_DELAY;
    send_param->len = sizeof(espnow_data_t);  // Start with header only
    send_param->buffer = malloc(ESPNOW_MAX_PAYLOAD);  // Max ESP-NOW packet size
    if (send_param->buffer == NULL) {
        ESP_LOGE("espnow_init", "Malloc send buffer fail");
        free(send_param);
        vSemaphoreDelete(espnow_send_queue);
        vSemaphoreDelete(espnow_recv_queue);
        esp_now_deinit();
        return ESP_FAIL;
    }
    memcpy(send_param->dest_mac, "\xFF\xFF\xFF\xFF\xFF\xFF", ESP_NOW_ETH_ALEN);
    espnow_data_prepare(send_param);

    // Increase stack sizes to prevent overflow

    if(IS_ROUTER){
        xTaskCreate(espnow_send_task_router, "espnow_send_task", 4096, send_param, 4, NULL);
        xTaskCreate(espnow_recv_task_router, "espnow_recv_task", 4096, NULL, 4, NULL);
    }else{
        xTaskCreate(espnow_send_task_device, "espnow_send_task", 4096, send_param, 4, NULL);
        xTaskCreate(espnow_recv_task_device, "espnow_recv_task", 4096, NULL, 4, NULL);
    }

    return ESP_OK;
}
