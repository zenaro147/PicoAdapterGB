#pragma once

// Centralized pin/UART/timeout configuration for the esp implementation, so
// no other file in this backend hardcodes a GPIO number or a magic timeout
// constant directly (see src/implementations/esp/README.md for the wiring
// this corresponds to).

// ESP8266EX / ESP-01 wiring (see README.md for the physical pin numbers):
//   Pico GP4 (UART1 TX) -> ESP RX
//   Pico GP5 (UART1 RX) <- ESP TX
// GP4/GP5 select UART1's primary function (GPIO_FUNC_UART) on both RP2040
// and RP2350 - confirmed from the Pico SDK's own GPIO function tables
// (hardware/gpio.h), not assumed from a datasheet read from memory.
#define ESP_UART_INSTANCE   uart1
// UART_IRQ_NUM() is the SDK's own platform-portable way to get the NVIC IRQ
// number for a uart_inst_t* (see hardware/uart.h); resolved wherever this is
// actually used (esp_uart.c, after hardware/uart.h is included there).
#define ESP_UART_IRQ        UART_IRQ_NUM(ESP_UART_INSTANCE)
#define ESP_UART_TX_PIN     4
#define ESP_UART_RX_PIN     5

// ESP-AT v2.3.0.0 factory-default UART configuration for the ESP8266 is
// 115200 8N1. This backend never sends AT+UART_CUR/AT+UART_DEF to change it
// (see esp_at_init()): reliability takes priority over throughput here, and
// changing the module's own UART config adds a failure mode (a
// misconfigured/already-changed module becoming unreachable) for no benefit
// this project needs. If a module was previously reconfigured to a different
// baud rate, it must be restored (AT+UART_DEF=115200,8,1,0,0 from a terminal,
// or AT+RESTORE) before use with this firmware.
#define ESP_UART_BAUD_RATE  115200

// AT+CIPMUX=1 gives us link IDs 0-4 (5 total, confirmed in ESP-AT TCP/IP AT
// command doc). MOBILE_MAX_CONNECTIONS (2, from libmobile) claims two fixed
// IDs for the Mobile Adapter's own sockets; the web config server (also
// capped at 2 simultaneous connections, see web/web_internal.h) uses
// whatever IDs AT+CIPSERVER's auto-accept assigns from the remaining pool.
// Reserving low, fixed IDs for the mobile sockets keeps their identity
// predictable across reconnects instead of depending on allocation order.
#define ESP_LINK_COUNT          5
#define ESP_LINK_MOBILE_BASE    0 // mobile socket i uses link (ESP_LINK_MOBILE_BASE + i)

// Command timeouts, in milliseconds. ESP-AT does not pipeline commands (only
// one is ever "in flight"), so every esp_at_cmd_*() call blocks the caller
// (not net_poll()'s own event processing - see esp_at.c) until one of these
// elapses or a terminal response line arrives.
#define ESP_AT_TIMEOUT_BASIC_MS       1000  // AT, ATE0, AT+CIPMUX, AT+CIPRECVMODE, ...
#define ESP_AT_TIMEOUT_WIFI_JOIN_MS  20000  // AT+CWJAP (association + DHCP)
#define ESP_AT_TIMEOUT_SOCKET_MS      5000  // AT+CIPSTART
#define ESP_AT_TIMEOUT_SEND_MS        5000  // AT+CIPSEND (prompt + SEND OK/FAIL)
#define ESP_AT_TIMEOUT_CLOSE_MS       3000  // AT+CIPCLOSE

// Boot/sync: how long to keep retrying a plain "AT" probe before giving up.
// There's no RESET/EN line to this module (ESP-01 wiring is RX/TX/VCC/GND
// only - see README.md), so this is the only way to find out the module is
// alive and to absorb whatever it prints on its own power-on (AT-firmware
// boot banner, garbage from a mismatched baud rate, etc.) before talking to
// it for real.
#define ESP_AT_SYNC_ATTEMPTS         10
#define ESP_AT_SYNC_ATTEMPT_DELAY_MS 500
