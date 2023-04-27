#pragma once

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include <stdio.h>
#include <string.h>

#include <mobile.h>

//UART pins
#define UART_ID uart1
#define BAUD_RATE 115200
#define UART_TX_PIN 4
#define UART_RX_PIN 5
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Basics AT functions

// RX interrupt handler
void on_uart_rx();

// Clean the RX buffer
void FlushATBuff();

// Find a string into the RX buffer
bool ESP_SerialFind(char * buf, char * target, int ms_timeout, bool cleanBuff, bool useMemFind);

// Send a AT command to the ESP
void ESP_SendCmd(uart_inst_t *uart, const uint8_t *command, int datasize);

// Return the command index inside the RX buffer (useful to get data from the RX to a variable)
int ESP_GetCmdIndexBuffer(char * rxbuff, char * cmdSearch);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Read the how much data remaining inside ESP buffer (only TCP)
int ESP_ReadBuffSize(uart_inst_t * uart, uint8_t connID);

// Retrieve data from the ESP buffer (only TCP) and store to the internal buffer (max Data Size = 2048)
int ESP_ReqDataBuff(uart_inst_t * uart, uint8_t connID, int dataSize);

// Send data to the Mobile Adapter GB Host
uint8_t ESP_SendData(uart_inst_t * uart, uint8_t connID, char * sock_type, char * conn_host, int conn_port, const void * databuff, int datasize);

// Establish a connection to the Host (Mobile Adapter GB server)
bool ESP_OpenSockConn(uart_inst_t * uart, uint8_t connID, char * sock_type, char * conn_host, int conn_port, int conn_localport, uint8_t conn_mode);

// Return the status of the desired ESP Socket
bool ESP_GetSockStatus(uart_inst_t * uart, uint8_t connID, void *user);

// Close the desired ESP Socket
bool ESP_CloseSockConn(uart_inst_t * uart, uint8_t connID);

//Open an ESP server to accept new connections
bool ESP_OpenServer(uart_inst_t * uart, uint8_t connID, int remotePort);

//Close an ESP server and all opened sockets
bool ESP_CloseServer(uart_inst_t * uart);

bool ESP_CheckIncommingConn(uart_inst_t * uart, uint8_t connID);

// Initialize the ESP-01 UART communication
bool EspAT_Init(uart_inst_t * uart, int baudrate, int txpin, int rxpin);

// Provides the necessary commands to connect the ESP to a WiFi network
bool ESP_ConnectWifi(uart_inst_t * uart, char * SSID_WiFi, char * Pass_WiFi, int timeout);
