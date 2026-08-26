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

// True if the RX ring buffer has dropped at least one byte (oldest-byte-
// dropped policy - see esp_uart.c) since the last call; clears the flag.
// esp_at.c uses this to detect that whatever was just fed to its line/
// payload parser may be desynced, so it can fail the in-flight command
// cleanly instead of treating post-drop bytes as if they still belonged to
// it (see esp_at_poll()).
bool esp_uart_take_overflow(void);

// Blocking write (AT commands/payloads are short and this is never called
// from the link-cable ISR, so this is safe and simpler than a TX ring buffer).
void esp_uart_write(const uint8_t *data, size_t len);
