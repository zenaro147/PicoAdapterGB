// SPDX-License-Identifier: GPL-3.0-only
#include "esp_uart.h"
#include "esp_config.h"

#include <assert.h>

#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"

// Power-of-two so the index wraps with a cheap mask. 512 bytes is generous
// for line-oriented AT traffic and for CIPRECVDATA responses, which this
// backend always requests in bounded chunks (see socket_impl_recv() -
// MOBILE_MAX_TRANSFER_SIZE is 254 bytes; CIPRECVDATA headers/trailers add at
// most a few dozen bytes of ASCII framing on top of that).
#define ESP_UART_RXBUF_SIZE 512
#define ESP_UART_RXBUF_MASK (ESP_UART_RXBUF_SIZE - 1)
static_assert((ESP_UART_RXBUF_SIZE & ESP_UART_RXBUF_MASK) == 0, "ESP_UART_RXBUF_SIZE must be a power of two");

static volatile uint8_t rx_buf[ESP_UART_RXBUF_SIZE];
static volatile uint16_t rx_head = 0; // written by IRQ
static volatile uint16_t rx_tail = 0; // read by esp_uart_read_byte()
static volatile bool rx_overflow = false;

static void esp_uart_irq_handler(void){
    while (uart_is_readable(ESP_UART_INSTANCE)) {
        uint8_t byte = uart_getc(ESP_UART_INSTANCE);
        uint16_t next_head = (rx_head + 1) & ESP_UART_RXBUF_MASK;
        if (next_head == rx_tail) {
            // Buffer full: drop the oldest byte rather than the newest, so a
            // slow consumer loses history instead of getting stuck unable to
            // ever see the tail end of whatever is currently arriving.
            rx_tail = (rx_tail + 1) & ESP_UART_RXBUF_MASK;
            rx_overflow = true;
        }
        rx_buf[rx_head] = byte;
        rx_head = next_head;
    }
}

void esp_uart_init(void){
    uart_init(ESP_UART_INSTANCE, ESP_UART_BAUD_RATE);
    gpio_set_function(ESP_UART_TX_PIN, UART_FUNCSEL_NUM(ESP_UART_INSTANCE, ESP_UART_TX_PIN));
    gpio_set_function(ESP_UART_RX_PIN, UART_FUNCSEL_NUM(ESP_UART_INSTANCE, ESP_UART_RX_PIN));
    uart_set_hw_flow(ESP_UART_INSTANCE, false, false);
    uart_set_format(ESP_UART_INSTANCE, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(ESP_UART_INSTANCE, true);

    irq_set_exclusive_handler(ESP_UART_IRQ, esp_uart_irq_handler);
    irq_set_enabled(ESP_UART_IRQ, true);
    uart_set_irq_enables(ESP_UART_INSTANCE, true, false);
}

bool esp_uart_read_byte(uint8_t *out){
    if (rx_tail == rx_head) return false;
    *out = rx_buf[rx_tail];
    rx_tail = (rx_tail + 1) & ESP_UART_RXBUF_MASK;
    return true;
}

bool esp_uart_readable(void){
    return rx_tail != rx_head;
}

bool esp_uart_take_overflow(void){
    bool v = rx_overflow;
    rx_overflow = false;
    return v;
}

void esp_uart_write(const uint8_t *data, size_t len){
    uart_write_blocking(ESP_UART_INSTANCE, data, len);
}
