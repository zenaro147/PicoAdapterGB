# PicoAdapterGB
Raspberry Pi Pico Code for Mobile Adapter GB emulation with real HW!

Based on: [Libmobile](https://github.com/REONTeam/libmobile) by [REON Team](https://github.com/REONTeam) and following the [BGB Implementation](https://github.com/REONTeam/libmobile-bgb)

### Current implementations
- `pico_esp8266_at`: A Raspberry Pi Pico implementation using an ESP8266 ESP-01 to provide WiFi connectivity. 
- `picow`: A Raspberry Pi Pico W implementation using the internal WiFi connectivity. 

Refer to the implementation's README.md for more details.

# Configuring the device
To configure your WiFi network and some other parameters, you need to put the device in "Config Mode". To do that, connect the device to a PC and, using a Serial Monitor, send any character when the text `Press any key to enter in Setup Mode...` shows.

When the message `Enter a command:` appears, you can enter the following commands below to set a value. To clear/reset the parameter, just enter the command without any value next, like this `WIFISSID=`.

The commands syntax is: `<COMMAND>=<VALUE>`, for example `WIFISSID=MY_WIFI`.

All commands are Case-Sensitive.

|   Command   | Effect |
|-------------|-------------|
| WIFISSID    | Set the SSID to use to connect. |
| WIFIPASS    | Set the password from the WiFi network to use to connect. |
| DNS1        | Set primary DNS that the adapter will use to parse the nameservers. |
| DNS2        | Set secondary DNS that the adapter will use to parse the nameservers. | 
| DNSPORT     | Set a custom DNS port to use with the DNS servers. |
| RELAYSERVER | Set a Relay Server that will be use during P2P communications. |
| RELAYTOKEN  | Set a Relay Token that will be used on Relay Server to receive a valid number to use during P2P communications. |
| P2PPORT     | Set a custom P2P port to use during P2P communications (Local Network only). |
| UNMETERED   | Set if the device will be Unmetered (useful for Pokemon Crystal). |
| EXIT        | Quit from Config Mode and Save the new values. If you change some value, the device will reboot. |
| HELP        | Show this command list on screen. |


-----------------------
### What need to do:
* Implement a native 32bits mode for GBA
* Clean code
