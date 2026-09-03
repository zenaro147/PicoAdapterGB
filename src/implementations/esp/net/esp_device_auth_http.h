#pragma once

// Internal to this backend - implements net_hal.h's
// net_device_auth_http_get_*() (see esp_device_auth_http.c). Must be polled
// every net_poll() to advance the connect/send/recv state machine.
void esp_device_auth_http_poll(void);
