
#include <string.h>
#include <esp_log.h>
#include "esp_ssd1306_driver.h"
#include "tasks/display.h"
#include "espnow_router_config.h"

extern QueueHandle_t display_queue;

void display_message(const char* msg, i2c_ssd1306_handle_t *i2c_ssd1306)
{
    // Display message under constraints of ssd1306 display; splitting words 
    // Number of rows and columns assumes 8x8 font
    size_t ssd1306_ncols = CONFIG_ESP_SSD1306_DRIVER_WIDTH / 8;
    size_t ssd1306_nrows = CONFIG_ESP_SSD1306_DRIVER_HEIGHT / 8;
    
    size_t len = strlen(msg);

    if(len < ssd1306_ncols){
        ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_ssd1306_buffer_text(i2c_ssd1306, 0, 0, msg, false));
        ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_ssd1306_buffer_to_ram(i2c_ssd1306));
    }
    else{
        size_t i = 0;
        char display_txt[ssd1306_ncols+1];
        int row_ind = 0;

        while(msg[i] != '\0'){
            display_txt[i % ssd1306_ncols] = msg[i];
            i++;

            // If display text has reached number of rows in display; send to display buffer
            if (i % ssd1306_ncols == 0 && i > 0){
                display_txt[ssd1306_ncols+1] = '\0';
                
                ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_ssd1306_buffer_text(i2c_ssd1306, 0, row_ind * 8, display_txt, false));
                // Increment display row 1 second and begin overwriting display
                row_ind++;
                // If all display rows are full, display for 1 second and then begining overwriting
                if (row_ind >= ssd1306_nrows){
                    ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_ssd1306_buffer_to_ram(i2c_ssd1306));
                    vTaskDelay(1000);
                    ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_ssd1306_buffer_fill(i2c_ssd1306, false));
                    ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_ssd1306_buffer_to_ram(i2c_ssd1306));
                    row_ind = 0;
                }               
            }
        }
        // Send remaining text to display buffer and display
        display_txt[i % ssd1306_ncols] = '\0';
        ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_ssd1306_buffer_text(i2c_ssd1306, 0, row_ind * 8, display_txt, false));
        ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_ssd1306_buffer_to_ram(i2c_ssd1306));
        // Clear display_txt memory after display
        memset(display_txt, 0, sizeof(display_txt));
    }
}

void display_message2(char* msg, i2c_ssd1306_handle_t *i2c_ssd1306)
{
    // Display message under constraints of ssd1306 display; do not split words unless longer than 16 letters
    // Number of rows and columns assumes 8x8 font
    size_t ssd1306_ncols = CONFIG_ESP_SSD1306_DRIVER_WIDTH / 8;
    size_t ssd1306_nrows = CONFIG_ESP_SSD1306_DRIVER_HEIGHT / 8;

    size_t len = strlen(msg);

    if(len < ssd1306_ncols){
        ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_ssd1306_buffer_text(i2c_ssd1306, 0, 0, msg, false));
        ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_ssd1306_buffer_to_ram(i2c_ssd1306));
        return;
    }

    // Process message in-place without storing all words
    char display_txt[ssd1306_ncols+1];
    display_txt[0] = '\0';
    int row_ind = 0;
    
    char *ptr = msg;
    char *word_start = msg;
    bool in_word = false;
    
    while (*ptr != '\0' || in_word) {
        // Find next word boundary
        if (*ptr == ' ' || *ptr == '\0') {
            if (in_word) {
                size_t word_len = ptr - word_start;
                
                // Handle words longer than display width
                if (word_len > ssd1306_ncols) {
                    // Display any buffered text first
                    if (strlen(display_txt) > 0) {
                        ESP_LOGI("display_message2", "display_txt: %s", display_txt);
                        ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_ssd1306_buffer_text(i2c_ssd1306, 0, row_ind * 8, display_txt, false));
                        display_txt[0] = '\0';
                        row_ind++;
                        if (row_ind >= ssd1306_nrows) {
                            ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_ssd1306_buffer_to_ram(i2c_ssd1306));
                            vTaskDelay(1000);
                            ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_ssd1306_buffer_fill(i2c_ssd1306, false));
                            ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_ssd1306_buffer_to_ram(i2c_ssd1306));
                            row_ind = 0;
                        }
                    }
                    
                    // Display long word in chunks
                    char *chunk = word_start;
                    while (chunk < ptr) {
                        size_t chunk_len = (ptr - chunk) > ssd1306_ncols ? ssd1306_ncols : (ptr - chunk);
                        snprintf(display_txt, sizeof(display_txt), "%.*s", (int)chunk_len, chunk);
                        ESP_LOGI("display_message2", "display_txt: %s", display_txt);
                        ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_ssd1306_buffer_text(i2c_ssd1306, 0, row_ind * 8, display_txt, false));
                        
                        chunk += ssd1306_ncols;
                        row_ind++;
                        if (row_ind >= ssd1306_nrows) {
                            ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_ssd1306_buffer_to_ram(i2c_ssd1306));
                            vTaskDelay(1000);
                            ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_ssd1306_buffer_fill(i2c_ssd1306, false));
                            ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_ssd1306_buffer_to_ram(i2c_ssd1306));
                            row_ind = 0;
                        }
                    }
                    display_txt[0] = '\0';
                }
                else {
                    // Normal word that fits
                    size_t current_len = strlen(display_txt);
                    size_t needed_space = (current_len > 0) ? 1 : 0; // Space before word
                    
                    if (current_len + needed_space + word_len < ssd1306_ncols) {
                        // Word fits on current line
                        if (current_len > 0) {
                            strcat(display_txt, " ");
                        }
                        strncat(display_txt, word_start, word_len);
                    }
                    else {
                        // Display current line and start new one
                        ESP_LOGI("display_message2", "display_txt: %s", display_txt);
                        ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_ssd1306_buffer_text(i2c_ssd1306, 0, row_ind * 8, display_txt, false));
                        row_ind++;
                        
                        if (row_ind >= ssd1306_nrows) {
                            ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_ssd1306_buffer_to_ram(i2c_ssd1306));
                            vTaskDelay(1000);
                            ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_ssd1306_buffer_fill(i2c_ssd1306, false));
                            ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_ssd1306_buffer_to_ram(i2c_ssd1306));
                            row_ind = 0;
                        }
                        
                        snprintf(display_txt, sizeof(display_txt), "%.*s", (int)word_len, word_start);
                    }
                }
                
                in_word = false;
            }
            
            if (*ptr == '\0') break;
            ptr++;
        }
        else {
            if (!in_word) {
                word_start = ptr;
                in_word = true;
            }
            ptr++;
        }
    }
    
    // Display remaining text
    if (strlen(display_txt) > 0) {
        ESP_LOGI("display_message2", "display_txt after while: %s", display_txt);
        ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_ssd1306_buffer_text(i2c_ssd1306, 0, row_ind * 8, display_txt, false));
    }
    
    ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_ssd1306_buffer_to_ram(i2c_ssd1306));
}

// RTOS task function for displaying messages in display queue
void display_task(void *pvParameters) 
{   
    
    i2c_ssd1306_handle_t *i2c_ssd1306 = (i2c_ssd1306_handle_t *)pvParameters;
    uint8_t data[BUF_SIZE];

    while (1) {
        // Wait indefinitely for a message 
        if (xQueueReceive(display_queue, &data, portMAX_DELAY) == pdTRUE) {
            // Clear display first
            ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_ssd1306_buffer_fill(i2c_ssd1306, false));
            // Then print to ssd1306 display
            display_message2((char*)data, i2c_ssd1306);
        }
        vTaskDelay(50);
    }
}
