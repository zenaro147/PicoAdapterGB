# PicoAdapterGB
Raspberry Pi Pico Code for Mobile Adapter GB emulation with real HW!

Based on: [Libmobile](https://github.com/REONTeam/libmobile) by [REON Team](https://github.com/REONTeam) and following the [BGB Implementation](https://github.com/REONTeam/libmobile-bgb)

### What works:
* ESP01 can connect to a WiFi Network
* Gameboy can recognize Pico as a Mobile Device
* Can read the Mobile Adapter config on flash (since Pico don't have an EEPROM)
* Save Mobile Trainer Config into Flash (eeprom-like)
* Mobile Trainer login (pop and smtp) and web page navigation 
* Pokemon Crystal Odd Egg event

### What need to do:
* Prepare the "flash first setup" on Pico to save the "eeprom-like" config
* Save the custom DNSs, P2P port and "unmetered" parameters on flash as well
* Insert a "CONFIG" text at the beginning of the flash (move other contents) to identify if the flash was already formatted. 
* Implement the "sock_accept" and "sock_listen" functions to communicate as P2P connection.
