# PicoAdapterGB - A Mobile Adapter GB hardware emulator based on RaspberryPi Pico

<a href="https://www.buymeacoffee.com/zenaro147" target="_blank"><img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me A Coffee" style="height: 60px !important;width: 217px !important;" ></a>

This is the first standalone implementation of [Mobile Adapter GB](https://bulbapedia.bulbagarden.net/wiki/Mobile_Game_Boy_Adapter)!
<br>It's based on [Libmobile](https://github.com/REONTeam/libmobile) by [REON Team](https://github.com/REONTeam) and it's totally free from any kind of sofware. You just need to power the adapter with any power source that's provide 5v (like a power bank, a phone using an OTG, add a battery circuit, etc), and make the first configuration (using any Serial Monitor on PC/Smartphone).

It's possible to connect to any [REON project](https://github.com/REONTeam/reon) custom server as soon it releases.

## Development Status
Last Update [![Release Date](https://img.shields.io/github/release-date-pre/zenaror/PicoAdapterGB?style=plastic)](#)<br>
* Stable Release [![Release Version](https://img.shields.io/github/v/release/zenaror/PicoAdapterGB?style=plastic)](https://github.com/zenaror/PicoAdapterGB/releases/latest/)
* Beta Release [![Beta Version](https://img.shields.io/github/v/release/zenaror/PicoAdapterGB?include_prereleases&sort=date&filter=beta-*)](https://github.com/zenaror/PicoAdapterGB/releases?q=beta-*)
* Bleeding Edge [![Bleeding Edge](https://img.shields.io/github/actions/workflow/status/zenaror/PicoAdapterGB/bleeding-edge.yml?branch=develop&style=plastic&label=Bleeding%20Edge)](https://github.com/zenaror/PicoAdapterGB/releases#release-bleeding-edge)

If you still have questions, ask us here or in the **REON Team Discord** [![Discord Group](https://img.shields.io/badge/chat-on%20Discord-738ADB)](https://discord.gg/mKT4pTfUqC)

## How to build
Refer to the [Wiki page](https://github.com/zenaror/PicoAdapterGB/tree/main/doc/wiki) for more details on how to build one according to your setup.

## Demo
[![Click on the image to see the full video](https://github.com/zenaror/PicoAdapterGB/blob/main/doc/demoPreview.gif)](https://youtu.be/YvNsaXxCjOU)<br> 
<sub>Click on the image to watch the full video</sub>

## Current implementations
- `picow`: A Raspberry Pi Pico W / Pico 2 W implementation using the internal WiFi connectivity. (recommended)
- `esp`: A Raspberry Pi Pico / Pico 2 implementation using an external ESP8266EX (ESP-01) module running ESP-AT for WiFi connectivity. See [src/implementations/esp/README.md](src/implementations/esp/README.md).

## 3D printed shell
* Check out the Hatch's [Thingverse Page](https://www.thingiverse.com/thing:7057318)

## Posts about:
* [Reddit post 1](https://www.reddit.com/r/Gameboy/comments/14scudy/just_dropping_this_mobile_adapter_gb_revival_wip/)
* [Reddit post 2](https://www.reddit.com/r/Gameboy/comments/16ly811/first_mobile_reon_adapter_working_pretty_good_now/)

## Licensing

PicoAdapterGB is released under the **GNU General Public License v3** (see [LICENSE](LICENSE)).

The firmware links [libmobile](https://github.com/REONTeam/libmobile), which is a separate
work by the REON Team, released under the **GNU Lesser General Public License v3 or later**
(`SPDX-License-Identifier: LGPL-3.0-or-later`). It is not copied into this tree: it is a git
submodule under `dependences/libmobile`, and it carries its own license texts there
(`COPYING` and `COPYING.LESSER`). The exact revision this firmware is built against is the
commit pinned by the submodule, and the build fetches it from the URL declared in
[.gitmodules](.gitmodules).

Combining the two is explicitly permitted: the LGPLv3 is the GPLv3 plus a set of additional
permissions, and GPLv3 §7 allows a recipient to drop those additional permissions, so
LGPLv3 code may be conveyed as part of a GPLv3 work. The result — the firmware image — is
therefore distributed under the GPLv3, while libmobile itself remains available to everyone
under the LGPLv3.

Practical consequences for anyone redistributing a built `.uf2`:

* ship or offer the complete corresponding source for both this project and the exact
  libmobile revision used;
* keep the copyright and license notices intact, including libmobile's SPDX headers;
* if you modify libmobile, publish those modifications — the upstream project asks for this
  explicitly, and it is the reason the library is LGPL rather than permissive.

libmobile's author also grants Nintendo and its subsidiaries use under the zero-clause BSD
license; that exemption is theirs to give and applies to libmobile only, not to this project.

## Credits 
* [REON Team](https://github.com/REONTeam/) - Mobile Adapter Library (libmobile, LGPLv3)
* [mid-kid](https://github.com/mid-kid/) - Help to structure the 32bits solution
* [Lorenzooone](https://github.com/Lorenzooone/) - GB Link cable PIO solution and better eeprom save structure
* [kabili207](https://github.com/kabili207/) - Better Stacksmashing build options 

