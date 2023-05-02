# PicoAdapterGB - A Mobile Adapter GB hardware emulator based on RaspberryPi Pico

Based on: [Libmobile](https://github.com/REONTeam/libmobile) by [REON Team](https://github.com/REONTeam)

## Development Status
Latest Stable Release  [![Release Version](https://img.shields.io/github/v/release/zenaro147/PicoAdapterGB?style=plastic)](https://github.com/zenaro147/PicoAdapterGB/releases/latest/)  [![Release Date](https://img.shields.io/github/release-date/zenaro147/PicoAdapterGB?style=plastic)](https://github.com/zenaro147/PicoAdapterGB/releases/latest/)

Latest Development Release  [![Release Version](https://img.shields.io/github/release/zenaro147/PicoAdapterGB/all.svg?style=plastic)](https://github.com/zenaro147/PicoAdapterGB/releases/) [![Release Date](https://img.shields.io/github/release-date-pre/zenaro147/PicoAdapterGB.svg?style=plastic)](https://github.com/zenaro147/PicoAdapterGB/releases/) 

If you still have questions, ask us here or in the **REON Team Discord** [![Discord Group](https://img.shields.io/badge/chat-on%20Discord-738ADB)](https://discord.gg/mKT4pTfUqC)

# Current implementations
- `pico_esp8266_at`: A Raspberry Pi Pico implementation using an ESP8266 ESP-01 to provide WiFi connectivity. 
- `picow`: A Raspberry Pi Pico W implementation using the internal WiFi connectivity. (recommended)

Refer to the implementation's README.md for more specific details.

# Link Cable Setup
```
 ___________
|  6  4  2  |
 \_5__3__1_/ (at cable, front view, Game boy side)

``` 
| Link Cable |Level Shifter|  Pico  | Notes |
|------------|-------------|---------| --- |
| Pin 1      |             |   N/A   | 5v from Game Boy (unnecessary) |
| Pin 2      |  HV1<->LV1  |   GPIO 19   | Serial Out (Game Boy side) |
| Pin 3      |  HV2<->LV2  |   GPIO 16   | Serial In (Game Boy side) |
| Pin 4      |             |   N/A   | Serial Data (unnecessary) |
| Pin 5      |  HV3<->LV3  |   GPIO 18   | Clock Out |
| Pin 6      |  GND<->GND  |   GND   | GND |
|            |      LV     |  +3.3V  | +3.3 volts from Pico (3v3 Out) |
|            |      HV     |   +5V   | +5 volts from Pico (VBUS) or Pin1 from Gameboy|

**⚠If this doesn't work, try to flip around Pin2(Serial Out) and Pin3(Serial In), as the pinout markings of your link cable breakout might be the other way around.⚠**

# Configuring the device
To configure your WiFi network and some other parameters, you need to put the device in "Config Mode". To do that, connect the device to a PC and, using a Serial Monitor at Baud rate 115200, send any character when the text `Press any key to enter in Setup Mode...` shows up.

The message `Enter a command:` appears, you can enter the following commands below to set a value. To clear/reset the parameter, just enter the command without any value next, like this `WIFISSID=`.

The commands syntax is: `<COMMAND>=<VALUE>`, for example `WIFISSID=MY_WIFI`.

All commands are Case-Sensitive.

|   Command   | Effect |
|-------------|-------------|
| WIFISSID      | Set the SSID to use to connect. |
| WIFIPASS      | Set the password from the WiFi network to use to connect. |
| DNS1          | Set primary DNS that the adapter will use to parse the nameservers. |
| DNS2          | Set secondary DNS that the adapter will use to parse the nameservers. | 
| DNSPORT       | Set a custom DNS port to use with the DNS servers. |
| RELAYSERVER   | Set a Relay Server that will be use during P2P communications. |
| RELAYTOKEN    | Set a Relay Token that will be used on Relay Server to receive a valid number to use during P2P communications. |
| P2PPORT       | Set a custom P2P port to use during P2P communications (Local Network only). |
| UNMETERED     | Set if the device will be Unmetered (useful for Pokemon Crystal). |

Special commands (just enter the command, without =<VALUE>):
|    Command    | Effect |
|---------------|-------------|
| FORMAT_EEPROM | Format the eeprom, if necessary. |
| EXIT          | Quit from Config Mode and Save the new values. If you change some value, the device will reboot. |
| HELP          | Show this command list on screen. |

-----------------------
### What need to do:
* Implement a native 32bits mode for GBA
