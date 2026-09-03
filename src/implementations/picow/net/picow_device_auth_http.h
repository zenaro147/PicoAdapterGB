#pragma once

// Internal to this backend - implements net_hal.h's
// net_device_auth_http_get_*() (see picow_device_auth_http.c). Must be
// polled every net_poll() to advance the connect/timeout state machine;
// actual data is handled by its own lwIP tcp callbacks during
// cyw43_arch_poll().
void picow_device_auth_http_poll(void);
