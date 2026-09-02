#pragma once

// Internal to this backend - implements net_hal.h's
// net_device_auth_resolve_*() (see picow_device_auth_dns.c). Must be polled
// every net_poll() to advance retry/timeout handling; actual DNS responses
// are handled by its own udp_recv() callback during cyw43_arch_poll().
void picow_device_auth_dns_poll(void);
