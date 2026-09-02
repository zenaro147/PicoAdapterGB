#pragma once

// Internal to this backend - implements net_hal.h's
// net_device_auth_resolve_*() (see esp_device_auth_dns.c). Must be polled
// every net_poll() to drain the pending recv and advance retry/timeout
// handling.
void esp_device_auth_dns_poll(void);
