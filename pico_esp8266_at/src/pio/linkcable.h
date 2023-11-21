#pragma once

#include "hardware/pio.h"

//#define STACKSMASHING

#define LINKCABLE_PIO       pio0
#define LINKCABLE_SM        0

#define LINKCABLE_BITS      8
#define LINKCABLE_BITS_FULL 32

#define CABLE_PINS_START    0

#define LINKCABLE_CAN_DISABLE_TIMEOUT SEC(1)

// Initialization for the handle_time_request
void init_time_request_handler(void);
// Multi-core compatible function for handling last_transfer_time requests
void handle_time_request(void);
// Receive the data from the linkcable
uint32_t linkcable_receive(void);
// Prepare the data to send through the linkcable, for the next transfer
void linkcable_send(uint32_t data);
// Make sure the linkcable is sending/receiving the newest data
void linkcable_flush(void);
// Change the linkcable's mode, if 32 bits is not available,
// set LIBMOBILE_ENABLE_NO32BIT and dummy this function out
void linkcable_set_is_32(bool is_32);
// Disable the linkcable. If in a multi-core setup,
// synchronization must be guaranteed by the caller,
// after the execution of this function
void linkcable_disable(void);
void linkcable_enable(void);
bool linkcable_is_enabled(void);
void linkcable_reset(bool re_enable);
// Initialize the linkcable with the function which will handle
// incoming and outgoing data
void linkcable_init(irq_handler_t onReceive);
bool can_disable_linkcable_handler(void);
// Debug function
void print_last_linkcable(void);