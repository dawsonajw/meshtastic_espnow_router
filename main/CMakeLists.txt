idf_component_register(SRC_DIRS "." "tasks"
                    INCLUDE_DIRS "." "tasks"
                    REQUIRES esp_ssd1306 esp_driver_gpio 
                    esp_driver_uart esp_driver_i2c esp_timer nvs_flash
                    esp_event esp_netif esp_wifi)
