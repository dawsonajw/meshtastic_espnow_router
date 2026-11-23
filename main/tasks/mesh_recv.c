#include <stdio.h>
#include <string.h>
#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <driver/uart.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include  "tasks/mesh_recv.h"
#include "espnow_router_config.h"

extern QueueHandle_t mesh_send_queue;
extern QueueHandle_t espnow_send_queue;
extern QueueHandle_t display_queue;
extern QueueHandle_t action_queue;

// RTOS task function for receiving meshtastic messages and sending to both action and display queues
void mesh_recv_task(void *pvParameters) 
{
    // Receive data
    uint8_t data[BUF_SIZE];
    while (1) {
        // Read data from UART
        int len;
        ESP_ERROR_CHECK(uart_get_buffered_data_len(UART_PORT_NUM, (size_t*)&len));
        len = uart_read_bytes(UART_PORT_NUM, data, len, 20 / portTICK_PERIOD_MS);

        if (len > 0) {
            data[len] = '\0'; // Null-terminate the string
            ESP_LOGI("mesh_recv_task", "UART Received len: %d; Msg: %s", len, (char*)data);
            
            // For some reason Meshtastic messages have two leading and two trailing zeros; remove them
            uint8_t sntextmsg[BUF_SIZE];  // Make it full size
            memcpy(sntextmsg, &data[2], (len - 4));
            sntextmsg[len-4] = '\0';
            //ESP_LOGI("UART", "sntextmsg: %s", sntextmsg);
            // Meshtastic TEXTMSG are formated as <Short Name>: <text>; Strip out <Short Name> and space after :
            //TO DO Add error catch to ensure sntextmsg is appropriately formatted;
            // Strip <Short Name>:
            char *colon_pos = strchr((char*)sntextmsg, ':');
            if (colon_pos != NULL) {
                // Create message buffer to send
                uint8_t textmsg[BUF_SIZE];
                strcpy((char*)textmsg, colon_pos + 2); // Skip ": "
                //ESP_LOGI("UART", "textmsg: %s", textmsg);
                
                // Send the message to the respective queues
                if (xQueueSend(action_queue, textmsg, 100 / portTICK_PERIOD_MS) != pdTRUE) {
                    ESP_LOGW("mesh_recv_task", "Send to process queue fail");
                }
                if (xQueueSend(display_queue, textmsg, 100 / portTICK_PERIOD_MS) != pdTRUE) {
                    ESP_LOGW("mesh_recv_task", "Send to display queue fail");
                }
                if (xQueueSend(espnow_send_queue, textmsg, 100 / portTICK_PERIOD_MS) != pdTRUE){
                    ESP_LOGW("mesh_recv_task", "Send to espnow send queue fail");
                }
            }
        }
        vTaskDelay(50);
    }
}

void mesh_send_task(void *pvParameters){

    uint8_t data[BUF_SIZE];

    while (1) {
        // Wait indefinitely for a message then write to UART to Meshtastic
        if (xQueueReceive(mesh_send_queue, &data, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI("mesh_send_task", "msg: %s", (char *)data);
            uart_write_bytes(UART_PORT_NUM, (const char*) data, strlen((char *)data));
        }
        vTaskDelay(3000/portTICK_PERIOD_MS);
    }
}