#pragma once

#include "hardware/pio.h"

//#define STACKSMASHING

#define LINKCABLE_PIO       pio0
#define LINKCABLE_SM        0

#define LINKCABLE_BITS      8
#define LINKCABLE_BITS_FULL 32

#define CABLE_PINS_START    0

void init_time_request_handler(void);
void handle_time_request(void);
uint32_t linkcable_receive(void);
void linkcable_send(uint32_t data);
void clean_linkcable_fifos(void);
void linkcable_set_is_32(bool is_32);
void linkcable_disable(void);
void linkcable_enable(void);
bool linkcable_is_enabled(void);
void linkcable_reset(bool re_enable);
void linkcable_init(irq_handler_t onReceive);
bool can_disable_linkcable_irq(void);
void print_last_linkcable(void);