#pragma once

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include <stdio.h>
#include <string.h>

#include <mobile.h>

#include "global.h"

//UART pins
#define UART_ID uart1
#define BAUD_RATE 115200
#define UART_TX_PIN 4
#define UART_RX_PIN 5

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void *memmem2 (const void *l, size_t l_len, const void *s, size_t s_len);

// Basics AT functions
void on_uart_rx();
void FlushATBuff();
bool ESP_SerialFind(char * buf, char * target, int ms_timeout, bool cleanBuff, bool useMemFind);
void ESP_SendCmd(uart_inst_t *uart, const uint8_t *command, int datasize);
int ESP_GetCmdIndexBuffer(char * rxbuff, char * cmdSearch);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ESP_CloseSockConn(uart_inst_t * uart, uint8_t connID);
bool ESP_OpenSockConn(uart_inst_t * uart, uint8_t connID, char * sock_type, char * conn_host, int conn_port, int conn_localport, uint8_t conn_mode);
bool ESP_GetSockStatus(uart_inst_t * uart, uint8_t connID);

int ESP_ReadBuffSize(uart_inst_t * uart, uint8_t connID);
int ESP_ReqDataBuff(uart_inst_t * uart, uint8_t connID, int dataSize);
uint8_t ESP_SendData(uart_inst_t * uart, uint8_t connID, char * sock_type, char * conn_host, int conn_port, const void * databuff, int datasize);
bool ESP_OpenServer(uart_inst_t * uart, uint8_t connID, int remotePort);
bool ESP_CloseServer(uart_inst_t * uart);
bool ESP_CheckIncommingConn(uart_inst_t * uart, uint8_t connID);
bool EspAT_Init(uart_inst_t * uart, int baudrate, int txpin, int rxpin);
bool ESP_ConnectWifi(uart_inst_t * uart, char * SSID_WiFi, char * Pass_WiFi, int timeout);
