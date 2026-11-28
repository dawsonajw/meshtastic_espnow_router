#include <driver/gpio.h>
#include <esp_check.h>
#include <esp_err.h>
#include "board_endpoints.h"
#include "protocol.h"
#include "espnow_router_config.h"

static const char *TAG = "board_endpoints";

#if(BOARD_TYPE == BASIC_LED_BOARD)
    // Define uart controlled GPIO pin; for now turns on LED 
    #define CONTROLLED_GPIO GPIO_NUM_27
    static const ep_local_config_t board_eps[] = {
        { .pin = CONTROLLED_GPIO, .ep_desc = {.ep_id = 0, .ep_type = EP_BINARY_ACTUATOR, 
            .unit = UNIT_NONE, .scale = 0, .name = "led"}},
    };
#else
    #error "Unknown BOARD_TYPE: set it in espnow_router_config.h"
#endif

#define BOARD_EP_COUNT(arr) (sizeof(arr) / sizeof((arr)[0]))
_Static_assert(BOARD_EP_COUNT(board_eps) <= ANNOUNCE_MAX_EPS, "board endpoints must be less than or equal to ANNOUNCE_MAX_EPS");

esp_err_t board_init(void){
    
    for(int i = 0; i < board_ep_count(); i++ ) {
        
        ESP_RETURN_ON_FALSE(board_eps[i].ep_desc.ep_type != EP_INVALID &&
                             board_eps[i].ep_desc.ep_type < EP_TYPE_COUNT, ESP_ERR_INVALID_STATE, 
                             TAG, "%s: endpoint %d: invalid type %d", __func__, i, board_eps[i].ep_desc.ep_type);
        switch (board_eps[i].ep_desc.ep_type){
            case EP_BINARY_SENSOR:
                return ESP_ERR_NOT_SUPPORTED;
            case EP_ANALOG_SENSOR:
                return ESP_ERR_NOT_SUPPORTED;
            case EP_BINARY_ACTUATOR:
                /* Configure controlled GPIO pin */
                ESP_RETURN_ON_ERROR(gpio_reset_pin(board_eps[i].pin), TAG, "%s: endpoint %d: pin %d not reset", __func__, i, board_eps[i].pin);
                /* Set the GPIO as a push/pull output */
                ESP_RETURN_ON_ERROR(gpio_set_direction(board_eps[i].pin, GPIO_MODE_OUTPUT), TAG, "%s: endpoint %d: pin %d direction not set", __func__, i, board_eps[i].pin);
                ESP_RETURN_ON_ERROR(gpio_set_level(board_eps[i].pin, 0), TAG, "%s: endpoint %d: pin %d level not set", __func__, i, board_eps[i].pin);
                break;
            case EP_LEVEL_ACTUATOR:
                return ESP_ERR_NOT_SUPPORTED;
            default:
                return ESP_ERR_INVALID_STATE;
        }
    }

    return ESP_OK;
}

esp_err_t board_announce(announce_msg_t *ann, size_t buf_size, size_t *out_size){

    ESP_RETURN_ON_FALSE(ann && out_size, ESP_ERR_INVALID_ARG, TAG, "%s: null argument", __func__);
    
    size_t size_req = (sizeof(announce_msg_t) + board_ep_count() *sizeof(endpoint_desc_t));
    ESP_RETURN_ON_FALSE(buf_size >= size_req, ESP_ERR_INVALID_SIZE, TAG, "%s: buffer undersized", __func__);
    
    ann->msg_type = MSG_ANNOUNCE;
    ann->ep_count = board_ep_count();

    for(int i = 0; i < board_ep_count(); i++){
        ann->eps[i] = board_eps[i].ep_desc;
    }
    *out_size = size_req;

    return ESP_OK;
}

esp_err_t board_get_ep_by_id(uint8_t ep_id, ep_local_config_t *out){

    
    return ESP_ERR_NOT_SUPPORTED;
}

uint8_t  board_ep_count(void){
    
    return (uint8_t) BOARD_EP_COUNT(board_eps);
}