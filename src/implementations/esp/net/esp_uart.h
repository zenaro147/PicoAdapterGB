#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// IRQ-driven UART transport to the ESP8266, sitting below esp_at.c's line/
// event parser. RX bytes are pushed into a ring buffer from the UART IRQ
// handler so that a byte the ESP sends spontaneously (a "+IPD"/"CLOSED"/
// "WIFI DISCONNECT" event, see esp_at.c) is never dropped just because the
// main loop is busy servicing the Game Boy link cable when it arrives (see
// PicoAdapterGB-Claude.md section 9's requirement).

void esp_uart_init(void);

// Non-blocking: returns false if the ring buffer is empty.
bool esp_uart_read_byte(uint8_t *out);

// True if at least one byte is currently buffered.
bool esp_uart_readable(void);

// Blocking write (AT commands/payloads are short and this is never called
// from the link-cable ISR, so this is safe and simpler than a TX ring buffer).
void esp_uart_write(const uint8_t *data, size_t len);
