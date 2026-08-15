#pragma once

// Backend-specific extras that only make sense for the cyw43+lwIP transport
// (used directly by web/, since the config server needs raw lwIP access).
// main.c and adapter/core code must not include this; use net/net_hal.h.
