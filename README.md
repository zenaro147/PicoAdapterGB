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
* Some method to apply new configurations, such as new SSID, password and DNS.

## How to setup the Hardware

### Esp8266 ESP-01

### Link Cable
```
 __________
|  6  4  2 |
 \_5__3__1_/ (at cable, front view, Game boy side)

``` 
| Link Cable |Level Shifter|  Pico  | Notes |
|------------|-------------|---------| --- |
| Pin 1      |             |   N/A   | 5v from Game Boy (unnecessary) |
| Pin 2      |  HV1<->LV1  |   G19   | Serial Out (Game Boy side) |
| Pin 3      |  HV2<->LV2  |   G23   | Serial In (Game Boy side) |
| Pin 4      |             |   N/A   | Serial Data (unnecessary) |
| Pin 5      |  HV3<->LV3  |   G18   | Clock Out |
| Pin 6      |  GND<->GND  |   GND   | GND |
|            |      LV     |  +3.3V  | +3.3 volts from Pico (3v3 Out) |
|            |      HV     |   +5V   | +5 volts from Pico (VBUS) |

**⚠If this doesn't work, try to flip around Pin2(Serial Out) and Pin3(Serial In), as the pinout markings of your link cable breakout might be the other way around.⚠**
