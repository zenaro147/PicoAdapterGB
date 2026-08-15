#pragma once

#include "globals.h"

// Starts the config HTTP server on port 80 and returns immediately. The
// caller's own loop must keep calling net_poll() (this module does not poll
// on its own). Does not touch adapter operation (mobile_loop/linkcable/sockets).
void web_config_start(struct mobile_user *mobile);

// Tears down the server. Safe to call once; it never restarts by itself.
void web_config_stop(void);

// Hotspot-fallback mode: blocks forever, polling the network stack itself.
// There's no valid network to fall back to, so the only way out is the user
// rebooting the device from the web page.
void web_config_run_blocking(struct mobile_user *mobile);
