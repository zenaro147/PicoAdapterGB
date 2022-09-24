# PicoAdapterGB
Raspberry Pi Pico Code for Mobile Adapter GB emulation with real HW!

Based on: [Libmobile](https://github.com/REONTeam/libmobile) by [REON Team](https://github.com/REONTeam) and following the [BGB Implementation](https://github.com/REONTeam/libmobile-bgb)

### What works (and I tested):
* ESP01 can connect to a WiFi Network
* Gameboy can recognize Pico as a Mobile Device
* Can read the Mobile Adapter config on flash (since Pico don't have an EEPROM)
* Save Mobile Trainer Config into Flash (eeprom-like)
* Mobile Trainer login (pop and smtp) and web navigation 
* Pokemon Crystal Odd Egg event

### What need to do:
* If necessary, save the custom P2P port and "unmetered" parameters on flash as well
