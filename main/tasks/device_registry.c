#include <ctype.h>
#include <esp_check.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>
#include <strings.h>
#include <tasks/device_registry.h>

static device_entry_t device_registry[REGISTRY_MAX_DEVICES];
static SemaphoreHandle_t registry_mutex;

static const char *TAG = "device_registry";

esp_err_t registry_init(void)
{
    if (registry_mutex != NULL) {
        return ESP_OK;
    }

    registry_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(registry_mutex, ESP_ERR_NO_MEM, __func__, "mutex create failed");
    
    memset(device_registry, 0, sizeof(device_registry));   // allows registry_init to clear 
    return ESP_OK;
}

esp_err_t registry_update(const uint8_t mac[ESP_NOW_ETH_ALEN], const announce_msg_t *ann_msg, int8_t rssi)
{
    ESP_RETURN_ON_FALSE(mac && ann_msg, ESP_ERR_INVALID_ARG, __func__, "null argument");

    /* Verify ep_count is within accepted range */
    uint8_t ep_count = ann_msg->ep_count;
    ESP_RETURN_ON_FALSE(ep_count <= ANNOUNCE_MAX_EPS, ESP_ERR_INVALID_SIZE, __func__,
                        "ep_count must be between 0 and %d", ANNOUNCE_MAX_EPS);

    esp_err_t ret = ESP_ERR_NO_MEM;
    xSemaphoreTake(registry_mutex, portMAX_DELAY);

    // 1. Existing entry for this MAC? Otherwise 2. first free slot.
    device_entry_t *slot = NULL;
    for (int i = 0; i < REGISTRY_MAX_DEVICES; i++){
        if (device_registry[i].in_use && memcmp(device_registry[i].mac, mac, ESP_NOW_ETH_ALEN) == 0){
            slot = &device_registry[i];
            break;
        }
    }

    if (slot == NULL){
        for (int i = 0; i < REGISTRY_MAX_DEVICES; i++){
            if (!device_registry[i].in_use){
                slot = &device_registry[i];
                break;
            }
        }
    }
    
    if (slot != NULL){
        memset(slot, 0, sizeof(*slot));
        slot->in_use = true;
        memcpy(slot->mac, mac, ESP_NOW_ETH_ALEN);
        slot->ep_count = ep_count;
        memcpy(slot->eps, ann_msg->eps, ep_count * sizeof(endpoint_desc_t));
        for (int e = 0; e < ep_count; e++) {
            slot->eps[e].name[EP_NAME_LEN - 1] = '\0';  // guarantee termination
        }
        slot->rssi = rssi;
        slot->last_seen_us = esp_timer_get_time();
        ret = ESP_OK;
    }

    xSemaphoreGive(registry_mutex);
    if (ret != ESP_OK){
        ESP_LOGW(TAG, "%s: registry full, dropping " MACSTR, __func__, MAC2STR(mac));
    }
    return ret;
}

esp_err_t registry_find_by_mac(const uint8_t mac[ESP_NOW_ETH_ALEN], device_entry_t *out)
{
    ESP_RETURN_ON_FALSE(mac && out, ESP_ERR_INVALID_ARG, __func__, "null argument");
    
    esp_err_t ret = ESP_ERR_NOT_FOUND;

    xSemaphoreTake(registry_mutex, portMAX_DELAY);

    for (int i = 0; i < REGISTRY_MAX_DEVICES; i++){
        if (device_registry[i].in_use && memcmp(device_registry[i].mac, mac, ESP_NOW_ETH_ALEN) == 0){
            *out = device_registry[i];
            ret = ESP_OK;
            break;
        }
    }
    
    xSemaphoreGive(registry_mutex);

    return ret;
}
    
esp_err_t registry_find_by_suffix(const char *hex, device_entry_t *out)
{
    ESP_RETURN_ON_FALSE(hex && out, ESP_ERR_INVALID_ARG, __func__, "null argument");

    /* Verify hex is the correct length and consists of hex digits*/
    size_t n = strlen(hex);
    ESP_RETURN_ON_FALSE(n >= 4 && n <= 2 * ESP_NOW_ETH_ALEN, ESP_ERR_INVALID_ARG, __func__,
                        "suffix must be 4-12 hex digits");

    for (size_t k = 0; k < n; k++) {
        ESP_RETURN_ON_FALSE(isxdigit((unsigned char)hex[k]), ESP_ERR_INVALID_ARG,
                            __func__, "not hex: %s", hex);
    }
    
    int matches = 0;

    xSemaphoreTake(registry_mutex, portMAX_DELAY);

    for (int i = 0; i < REGISTRY_MAX_DEVICES; i++){
        if (!device_registry[i].in_use) {
            continue;
        }
        const uint8_t *m = device_registry[i].mac;
        char mac_hex[2 * ESP_NOW_ETH_ALEN + 1]; // 12 hex digits + '\0'
        snprintf(mac_hex, sizeof mac_hex, "%02x%02x%02x%02x%02x%02x",
                 m[0], m[1], m[2], m[3], m[4], m[5]);

        if(strcasecmp(mac_hex + 2 * ESP_NOW_ETH_ALEN, hex) == 0){
            if (matches == 0)
            {
                *out = device_registry[i];
            }
            matches++;
        }
    }
    xSemaphoreGive(registry_mutex);

    if (matches == 0) return ESP_ERR_NOT_FOUND;
    if (matches > 1) return ESP_ERR_INVALID_STATE; // ambiguous: more digits needed
    return ESP_OK;

}

esp_err_t registry_next(int *cursor, device_entry_t *out)
{
    ESP_RETURN_ON_FALSE(cursor && out, ESP_ERR_INVALID_ARG, __func__, "null argument");

    /* Verify cursor is a valid position */
    ESP_RETURN_ON_FALSE(*cursor >= 0 && *cursor <= REGISTRY_MAX_DEVICES, ESP_ERR_INVALID_SIZE, __func__,
                        "cursor must be between 0 and %d", REGISTRY_MAX_DEVICES);
    
    esp_err_t ret = ESP_ERR_NOT_FOUND;

    xSemaphoreTake(registry_mutex, portMAX_DELAY);

    for (int i = *cursor; i < REGISTRY_MAX_DEVICES; i++){
        if (!device_registry[i].in_use) {
            continue;
        }
        *out = device_registry[i];
        *cursor = i + 1;
        ret = ESP_OK;
        break;
    }
    
    xSemaphoreGive(registry_mutex);

    return ret;
}

esp_err_t registry_log(void){
    device_entry_t device;
    int cursor = 0;
    int device_count = 0;

    ESP_RETURN_ON_FALSE(registry_mutex, ESP_ERR_INVALID_STATE, __func__, "device registry not initialized");

    esp_err_t ret = ESP_OK;
    
    while(ret == ESP_OK){
        ret = registry_next(&cursor, &device);
        if(ret == ESP_OK){
            ESP_LOGI(TAG, "%s: " MACSTR " # Endpoints: %d", __func__, MAC2STR(device.mac), device.ep_count);
            device_count++;
        }
    }
    
    ESP_LOGI(TAG, "%s: %d total devices in registry", __func__, device_count);

    if (ret == ESP_ERR_NOT_FOUND){
        return ESP_OK;
    }
    return ret;
}