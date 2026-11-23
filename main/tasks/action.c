#include <string.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <esp_now.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "tasks/action.h"
#include "espnow_router_config.h"
#include "espnow_config.h"

extern QueueHandle_t mesh_send_queue;
extern QueueHandle_t espnow_send_queue;
extern QueueHandle_t action_queue;

// RTOS task function for taking action based on messages in action queue
void action_task(void *pvParameters)
{
    // Print MAC Address
    static uint8_t mac[6];
    esp_err_t mac_ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char mac_string[20];
    if (mac_ret == ESP_OK) {
        sprintf(mac_string, "%02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        ESP_LOGI("action_task", "MAC Address: %s", mac_string);
    }
    
    uint8_t data[BUF_SIZE];

    // Configure uart controlled LED
    gpio_reset_pin(CONTROLLED_GPIO);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(CONTROLLED_GPIO, GPIO_MODE_OUTPUT);
    
    char* led_state_str = "LED Off\0";
    char action_ack[BUF_SIZE];

    while (1) {
        // Wait indefinitely for a message
        if (xQueueReceive(action_queue, &data, portMAX_DELAY) == pdTRUE) {
            
            // Turn on LED if textmsg contains LED On
            if (strstr((char*)data, "LED On")){
                gpio_set_level(CONTROLLED_GPIO, 1);
                led_state_str = "LED On\0";
                action_ack[0] = '\0';
                strcpy(action_ack, mac_string);
                strcat(action_ack, led_state_str);
                
                // If Router, send action ACK via Meshtastic; If Device, send action ACK via ESPNOW
                if(IS_ROUTER){
                    if (xQueueSend(mesh_send_queue, action_ack, 100 / portTICK_PERIOD_MS) != pdTRUE){
                        ESP_LOGW("action_task", "Send to mesh send queue fail");
                    }
                }
                else{
                    if (xQueueSend(espnow_send_queue, action_ack, 100 / portTICK_PERIOD_MS) != pdTRUE){
                        ESP_LOGW("action_task", "Send to espnow send queue fail");
                    }
                }
            }
            // Turn off LED if textmsg contains LED On
            else if(strstr((char*)data, "LED Off")){
                gpio_set_level(CONTROLLED_GPIO, 0);
                led_state_str = "LED Off\0";
                action_ack[0] = '\0';
                strcpy(action_ack, mac_string);
                strcat(action_ack, led_state_str);

                // If Router, send action ACK via Meshtastic; If Device, send action ACK via ESPNOW
                if(IS_ROUTER){
                    if (xQueueSend(mesh_send_queue, action_ack, 100 / portTICK_PERIOD_MS) != pdTRUE){
                        ESP_LOGW("action_task", "Send to mesh send queue fail");
                    }
                }
                else{
                    if (xQueueSend(espnow_send_queue, action_ack, 100 / portTICK_PERIOD_MS) != pdTRUE){
                        ESP_LOGW("action_task", "Send to espnow send queue fail");
                    }
                }
            }
        }
        vTaskDelay(50);
    }
}
