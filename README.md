# [W.I.P.] PicoAdapterGB
Raspberry Pi Pico Code for Mobile Adapter GB emulation with real HW!

Based on: [Libmobile](https://github.com/REONTeam/libmobile-atmega) by [REON Team](https://github.com/REONTeam) and following the [BGB Implementation](https://github.com/REONTeam/libmobile-bgb) and [Atmega Implementation](https://github.com/REONTeam/ArduinoAdapterGB)

### What works:
* ESP01 can connect to a WiFi Network
* Gameboy can recognize Pico as a Mobile Device
* Can read the Mobile Adapter config on flash (since Pico don't have an EEPROM)
* The Pokemon Crystal sefl-trade using Mobile Adapter (just to test the trade function)

### What need to do:
* Find the right time to write the config into flash again
* Implement all mobile_board_sock_* functions
* Basically, everything!
