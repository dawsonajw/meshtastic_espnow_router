#ifndef ESPNOW_ROUTER_CONFIG_H
#define ESPNOW_ROUTER_CONFIG_H

#include <esp_now.h>

// Onboard LED parameters
#define BLINK_GPIO 2
#define BLINK_PERIOD 250

// Define uart controlled GPIO pin; for now turns on LED 
#define CONTROLLED_GPIO 27

// Define UART parameters
#define UART_PORT_NUM      UART_NUM_2  // Use UART1
#define UART_BAUD_RATE     115200      // Set baud rate
#define TXD_PIN            17 // TX pin
#define RXD_PIN            16 // RX pin
#define BUF_SIZE           (256 * 2)        // Buffer size

#define QUEUE_SIZE 5 // Size of mesh_send_queue, espnow_recv_queue, espnow_send_queue, display_queue, action_queue

// Configure Router vs Device
#define IS_ROUTER 1
//#define ROUTER_MAC {0x28, 0x05, 0xa5, 0x09, 0x74, 0x08}; 
#define ROUTER_MAC "\x28\x05\xa5\x09\x74\x08" // D1 MAC
#define BROADCAST_MAC "\xFF\xFF\xFF\xFF\xFF\xFF"

#endif