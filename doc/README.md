# PicoAdapterGB Documentation

PicoAdapterGB is a Raspberry Pi Pico / Pico W implementation of the Nintendo Mobile Adapter GB protocol. Its goal is to reproduce the behavior of the original adapter closely enough for real Game Boy hardware and software to communicate with it over the link cable and the network.

This project combines:

- Game Boy link-cable protocol handling
- Mobile Adapter GB logic and state machine
- Wi-Fi networking, either onboard (Pico W / Pico 2 W) or via an external ESP8266EX running ESP-AT (plain Pico / Pico 2)
- Web-based configuration
- EEPROM-backed configuration persistence

## Project layout

- `src/` — core firmware: Game Boy link cable (PIO), libmobile glue, storage, and the network/LED interfaces every board target implements
- `src/implementations/picow/` — Pico W / Pico 2 W backend (cyw43 + lwIP), including the optional web setup UI
- `src/implementations/esp/` — Pico / Pico 2 + ESP8266EX (ESP-01, ESP-AT) backend, including the optional web setup UI (see its own [README.md](../src/implementations/esp/README.md))
- `dependences/libmobile/` — local copy of the libmobile protocol implementation
- `doc/` — project documentation
- `PicoAdapter_PCB/` — board and schematic files

See [doc/ARCHITECTURE.md](ARCHITECTURE.md) for the detailed module breakdown (network abstraction, adapter glue code, web server, storage) and how they depend on each other.

## Recommended hardware

- Raspberry Pi Pico W or Pico 2 W (`picow` implementation), or a plain Raspberry Pi Pico / Pico 2 plus an ESP8266EX (ESP-01) running ESP-AT (`esp` implementation)
- Bidirectional level shifter for Game Boy link-cable signals
- Game Boy link cable
- 5V power source capable of powering the adapter

## Firmware overview

The current recommended firmware target is the `picow` implementation (`src/implementations/picow/`), selected by default via `PICOADAPTER_IMPLEMENTATION=picow`. An alternative `esp` implementation (`src/implementations/esp/`) runs on a plain Pico/Pico 2 with an external ESP8266EX module for connectivity instead of the Pico W's onboard CYW43 - select it via `PICOADAPTER_IMPLEMENTATION=esp`.

On boot, the device:

1. initializes the Pico peripherals
2. loads saved configuration from EEPROM
3. connects to Wi-Fi using the stored SSID/password
4. starts the web setup interface if Wi-Fi is unavailable or not yet configured
5. waits for Game Boy communication
6. stops the web server as soon as the Game Boy begins talking

## Supported options

The build is configured through three independent CMake options:

- `PICO_BOARD` — `pico_w`/`pico2_w` (for `picow`), or `pico`/`pico2` (for `esp`)
- `PICOADAPTER_IMPLEMENTATION` — `picow` (default) or `esp`
- `ADAPTER` — `REON` or `STACKSMASHING`

These are defined in the top-level `CMakeLists.txt`.

## Build instructions

See [doc/BUILDING.md](BUILDING.md) for the exact build flow and command examples.

## Configuration

See [doc/CONFIGURATION.md](CONFIGURATION.md) for the current web configuration flow, Wi-Fi setup, and troubleshooting notes.

## Legacy / fallback notes

Older documentation in the wiki described a serial-based setup flow. The current firmware is primarily configured through the built-in web interface. The serial menu remains a useful fallback for troubleshooting, but the recommended path is the web setup UI.
