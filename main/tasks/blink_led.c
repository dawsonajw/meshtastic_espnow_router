#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include "tasks/blink_led.h"
#include "espnow_router_config.h"

// RTOS Task function for blinking onboard LED
void blink_led_task(void *pvParameters)
{
    // Configure onboard LED blink
    gpio_reset_pin(BLINK_GPIO);
    //Set the GPIO as a push/pull output
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
    while(1){
        gpio_set_level(BLINK_GPIO, 0);
        vTaskDelay(BLINK_PERIOD / portTICK_PERIOD_MS);
        gpio_set_level(BLINK_GPIO, 1);
        vTaskDelay(BLINK_PERIOD / portTICK_PERIOD_MS);
    }
}