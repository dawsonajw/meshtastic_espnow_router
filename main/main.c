#include <stdio.h>
#include <string.h>
#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <driver/uart.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "esp_ssd1306_driver.h"

#include <time.h>
#include <assert.h>
#include <freertos/semphr.h>
#include <freertos/timers.h>
#include "nvs_flash.h"
#include "esp_random.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "tasks/action.h"
#include "tasks/blink_led.h"
#include "tasks/display.h"
#include "tasks/espnow_router.h"
#include "tasks/mesh_recv.h"
#include "tasks/device_registry.h"

#include "board_endpoints.h"
#include "espnow_config.h"
#include "espnow_router_config.h"

// Meshtastic messages received by connected esp32 device are routed to other esp32 devices, processed for action, and displayed
QueueHandle_t mesh_send_queue; // Queue of meshtastic messages to send 
QueueHandle_t espnow_recv_queue; // Processed, sent to display_queue (eventually to mesh_send_queue)
QueueHandle_t espnow_send_queue; // Messages to send to other devices
QueueHandle_t display_queue;
QueueHandle_t action_queue;

// ESP32 Logo for display at start up
static const uint8_t ssd1306_esp_logo_img[4][32] = {
    {0x00, 0x00, 0x00, 0xC0, 0x60, 0x18, 0x00, 0x00, 0x70, 0x78, 0x78, 0x78, 0xF8, 0xF8, 0xF0, 0xF0,
     0xF2, 0xE6, 0xE6, 0xCE, 0x9E, 0x9C, 0x3C, 0x78, 0xF8, 0xF0, 0xE0, 0xC0, 0x80, 0x00, 0x00, 0x00},
    {0x00, 0xFC, 0x07, 0x60, 0xF8, 0xFC, 0xFE, 0xFE, 0x9E, 0x9E, 0x9E, 0x3E, 0x3E, 0x7C, 0x7C, 0xF9,
     0xF9, 0xF3, 0xE7, 0xCF, 0x9F, 0x3F, 0x7F, 0xFE, 0xFC, 0xF1, 0xE3, 0x8F, 0x1F, 0xFE, 0xF8, 0x00},
    {0x00, 0x07, 0x3C, 0xE0, 0x81, 0x03, 0x07, 0xC7, 0xE7, 0xC7, 0xCF, 0x1F, 0x7F, 0xFE, 0xFC, 0xF8,
     0xE1, 0x07, 0x3F, 0xFF, 0xFF, 0xFE, 0xF0, 0x01, 0x0F, 0xFF, 0xFF, 0xFF, 0x3C, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x01, 0x03, 0x06, 0x0D, 0x19, 0x11, 0x30, 0x20, 0x24, 0x4F, 0x4F, 0x4F,
     0x4F, 0x40, 0x40, 0x4F, 0x4F, 0x6F, 0x27, 0x20, 0x10, 0x10, 0x08, 0x0C, 0x04, 0x00, 0x00, 0x00},
};

// I2C Master configuration
static const i2c_master_bus_config_t i2c_master_bus_config = {
    .i2c_port = I2C_NUM_0,
    .scl_io_num = GPIO_NUM_22,
    .sda_io_num = GPIO_NUM_21,
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt = 7,
    .flags.enable_internal_pullup = true};

static i2c_master_bus_handle_t i2c_master_bus;

// SSD1306 configuration
/*static const i2c_ssd1306_config_t i2c_ssd1306_config = {
    .i2c_device_address = 0x3C,
    .i2c_scl_speed_hz = 400000,
    .width = 128,
    .height = 64,
    .wise = SSD1306_TOP_TO_BOTTOM};*/
static i2c_ssd1306_handle_t i2c_ssd1306;

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Create blinking onboard led rtos task
    xTaskCreate(&blink_led_task, "onboard_blink", 2048, NULL, 5, NULL);

    // Configure I2C and ssd1306 display
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_master_bus_config, &i2c_master_bus));
    ESP_ERROR_CHECK(i2c_ssd1306_init(i2c_master_bus, &i2c_ssd1306));
    ESP_ERROR_CHECK(i2c_ssd1306_clear(&i2c_ssd1306));
    ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_ssd1306_buffer_image(&i2c_ssd1306, 48, 16, (const uint8_t *)ssd1306_esp_logo_img, 32, 32, false));
    ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_ssd1306_buffer_to_ram(&i2c_ssd1306));

    // Configure UART parameters
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    QueueHandle_t uart2_queue;
    // Install UART driver using an event queue here if needed
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT_NUM, BUF_SIZE, BUF_SIZE, 10, &uart2_queue, 0));
    // Configure UART parameters
    ESP_ERROR_CHECK(uart_param_config(UART_PORT_NUM, &uart_config));
    // Set UART pins
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT_NUM, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    vTaskDelay(3000 / portTICK_PERIOD_MS);

    // Initial display and action queue
    mesh_send_queue = xQueueCreate(QUEUE_SIZE, BUF_SIZE);
    action_queue = xQueueCreate(QUEUE_SIZE, BUF_SIZE);
    display_queue = xQueueCreate(QUEUE_SIZE, BUF_SIZE);

    wifi_init();
    espnow_init();
    registry_init();
    registry_log();

    // Create tasks
    if (IS_ROUTER){
        // Register router device with router
        uint8_t ann_buf[sizeof(announce_msg_t) + ANNOUNCE_MAX_EPS * sizeof(endpoint_desc_t)] = {0};
        announce_msg_t *ann = (announce_msg_t *)ann_buf;
        size_t out_size = 0;
        ESP_ERROR_CHECK(board_announce(ann, sizeof(ann_buf), &out_size));

        uint8_t  mac[ESP_NOW_ETH_ALEN];
        ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
        ESP_ERROR_CHECK(registry_update(mac, ann, 0));

        registry_log();

        xTaskCreate(&mesh_recv_task, "mesh_recv", 4096, NULL, 1, NULL);
        xTaskCreate(&mesh_send_task, "mesh_send", 4096, NULL, 1, NULL);
    }
    xTaskCreate(&action_task, "actions", 4096, NULL, 1, NULL);
    xTaskCreate(&display_task, "display", 4096, &i2c_ssd1306, 5, NULL);
}