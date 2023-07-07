# PicoAdapterGB - A Mobile Adapter GB hardware emulator based on RaspberryPi Pico

<a href="https://www.buymeacoffee.com/zenaro147" target="_blank"><img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me A Coffee" style="height: 60px !important;width: 217px !important;" ></a>

This is the first standalone implementation of [Mobile Adapter GB](https://bulbapedia.bulbagarden.net/wiki/Mobile_Game_Boy_Adapter)!
<br>It's based on [Libmobile](https://github.com/REONTeam/libmobile) by [REON Team](https://github.com/REONTeam) and it's totally free from any kind of sofware. You just need to power the adapter with any power source that's provide 5v (like a power bank, a phone using an OTG, add a battery circuit, etc), and make the first configuration (using any Serial Monitor on PC/Smartphone).

It's possible to connect to any [REON project](https://github.com/REONTeam/reon) custom server as soon it releases.

## Development Status
Latest Stable Release  [![Release Version](https://img.shields.io/github/v/release/zenaro147/PicoAdapterGB?style=plastic)](https://github.com/zenaro147/PicoAdapterGB/releases/latest/)  [![Release Date](https://img.shields.io/github/release-date/zenaro147/PicoAdapterGB?style=plastic)](https://github.com/zenaro147/PicoAdapterGB/releases/latest/)
<br>Latest Development Release  [![Release Version](https://img.shields.io/github/release/zenaro147/PicoAdapterGB/all.svg?style=plastic)](https://github.com/zenaro147/PicoAdapterGB/releases/) [![Release Date](https://img.shields.io/github/release-date-pre/zenaro147/PicoAdapterGB.svg?style=plastic)](https://github.com/zenaro147/PicoAdapterGB/releases/) 

If you still have questions, ask us here or in the **REON Team Discord** [![Discord Group](https://img.shields.io/badge/chat-on%20Discord-738ADB)](https://discord.gg/mKT4pTfUqC)

# Current implementations
- `pico_esp8266_at`: A Raspberry Pi Pico implementation using an ESP8266 ESP-01 to provide WiFi connectivity. 
- `picow`: A Raspberry Pi Pico W implementation using the internal WiFi connectivity. (recommended)

Refer to the [Wiki page](https://github.com/zenaro147/PicoAdapterGB/wiki) for more details on how to build one.

# Demo
[Watch here!](https://youtu.be/YvNsaXxCjOU)

# Posts about:
* [Reddit post](https://www.reddit.com/r/Gameboy/comments/14scudy/just_dropping_this_mobile_adapter_gb_revival_wip/?sort=new)


-----------------------
### Next steps:
* Implement a native 32bits mode for GBA
* Better/dynamic buffer to handle TCP data

