#pragma once

#include "globals.h"

// Wires up all mobile_def_*() callbacks (serial, config storage, timers,
// sockets, number updates) onto the given adapter. Keeps main.c free of
// libmobile glue code.
void adapter_bridge_register_callbacks(struct mobile_adapter *adapter);

// Set by the serial_enable callback so the Game Boy ISR knows which transfer
// width (8 vs 32 bit) to use. Read from the time-sensitive ISR in main.c.
extern bool isLinkCable32;

// True once impl_config_write() has changed a byte not yet flushed to flash.
bool adapter_bridge_has_pending_config_write(void);
void adapter_bridge_clear_pending_config_write(void);
user_time_t adapter_bridge_last_config_edit_time(void);
