#pragma once
#include "hardware/flash.h"

#include "globals.h"

struct saved_data_pointers {
    uint8_t* eeprom;
    char* wifiSSID;
    char* wifiPASS;
};

void InitSave(void);
void InitSavedPointers(struct saved_data_pointers* saved_ptrs, struct mobile_user* mobile);
void ReadEeprom(uint8_t* buffer);
void ReadConfig(struct saved_data_pointers* save_ptrs);
void SaveConfig(struct saved_data_pointers* save_ptrs);

uint64_t read_big_endian(const uint8_t* buffer, size_t size);
void write_big_endian(uint8_t* buffer, uint64_t data, size_t size);

uint16_t calc_checksum(const uint8_t* buffer, uint32_t size);
void set_checksum(const uint8_t* buffer, uint32_t size, uint8_t* checksum_buffer);
bool check_checksum(const uint8_t* buffer, uint32_t size, const uint8_t* checksum_buffer);