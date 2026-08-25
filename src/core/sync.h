#pragma once
#include <stdbool.h>

// This all could be achieved with mutexes and proper primitives,
// but it would be slower

typedef volatile bool sync_t;

void set_core_shared(bool is_same_core);

void init_sync(sync_t* ack);
void ack_sync_req(sync_t* ack);
bool is_sync_req(sync_t* ack);
bool wait_for_sync(sync_t* ack);