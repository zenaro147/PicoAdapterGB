#pragma once

#include "web_internal.h"

// Route handlers, dispatched by web_http.c's web_dispatch(). Split by concern
// so each file only needs to know about its own slice of the API.

void handle_get_config(struct web_conn *c);
void handle_post_config(struct web_conn *c, const char *body);

void handle_get_eeprom(struct web_conn *c);
void handle_post_eeprom(struct web_conn *c, const char *body, int content_length);

void handle_post_format(struct web_conn *c);
void handle_post_reboot(struct web_conn *c);
