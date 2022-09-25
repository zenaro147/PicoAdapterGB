# PicoAdapterGB
Raspberry Pi Pico Code for Mobile Adapter GB emulation with real HW!

Based on: [Libmobile](https://github.com/REONTeam/libmobile) by [REON Team](https://github.com/REONTeam) and following the [BGB Implementation](https://github.com/REONTeam/libmobile-bgb)

### Current implementations
- `pico_esp8266_at`: A Raspberry Pi Pico implementation using an ESP8266 ESP-01 to provide WiFi connectivity. 

Refer to the implementation's README.md for more details.

-----------------------
### What need to do:
* If necessary, save the custom P2P port and "unmetered" parameters on flash as well
* Some method to apply new configurations, such as new SSID, password and DNS.
* Implement/test 32bits mode
