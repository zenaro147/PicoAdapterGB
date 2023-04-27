#pragma once

#define BUFF_AT_SIZE 2048 //2048 is the maximun you can receive from esp01

//UART RX Buffer Config
extern uint8_t buffATrx[BUFF_AT_SIZE+64]; // + extra bytes to hold the AT command answer echo
extern int buffATrx_pointer;
extern uint8_t buffRecData[BUFF_AT_SIZE];