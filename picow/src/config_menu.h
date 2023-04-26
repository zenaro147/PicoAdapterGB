#pragma once

#include "pico/stdlib.h"

#include <mobile.h>
#include "gblink.h"

int main_parse_addr(struct mobile_addr *dest, char *argv);
void main_set_port(struct mobile_addr *dest, unsigned port);
static bool main_parse_hex(unsigned char *buf, char *str, unsigned size);

bool FindCommand(char * buf, char * target);
void BootMenuConfig(void *user, char * wifissid, char * wifipass);