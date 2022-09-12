# [W.I.P.] PicoAdapterGB
Raspberry Pi Pico Code for Mobile Adapter GB emulation with real HW!

Based on: [Libmobile](https://github.com/REONTeam/libmobile-atmega) by [REON Team](https://github.com/REONTeam) and following the [BGB Implementation](https://github.com/REONTeam/libmobile-bgb) and [Atmega Implementation](https://github.com/REONTeam/libmobile-atmega)

### What works:
* ESP01 can connect to a WiFi Network
* Gameboy can recognize Pico as a Mobile Device
* Can read the Mobile Adapter config on flash (since Pico don't have an EEPROM)
* The Pokemon Crystal sefl-trade using Mobile Adapter (just to test the trade function)
* Save Mobile Trainer Config into Flash (eeprom-like)

### What need to do:
* Implement all mobile_board_sock_* functions to communicate with remote servers
* Basically, everything!😅
