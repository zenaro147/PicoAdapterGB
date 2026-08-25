#include "sync.h"
#include "globals.h"

static bool shared_same_core = true;

void set_core_shared(bool is_same_core) {
    shared_same_core = is_same_core;
}

void TIME_SENSITIVE(init_sync)(sync_t* ack) {
    *ack = true;
}

void TIME_SENSITIVE(ack_sync_req)(sync_t* ack) {
    *ack = true;
}

bool TIME_SENSITIVE(is_sync_req)(sync_t* ack) {
    return !(*ack);
}

bool TIME_SENSITIVE(wait_for_sync)(sync_t* ack) {
    if(!shared_same_core) {
        *ack = false;
        while(!(*ack));
        return true;
    }
    return false;
}