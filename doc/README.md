# PicoAdapterGB Documentation

PicoAdapterGB is a Raspberry Pi Pico / Pico W implementation of the Nintendo Mobile Adapter GB protocol. Its goal is to reproduce the behavior of the original adapter closely enough for real Game Boy hardware and software to communicate with it over the link cable and the network.

This project combines:

- Game Boy link-cable protocol handling
- Mobile Adapter GB logic and state machine
- Pico W networking and Wi-Fi
- Web-based configuration
- EEPROM-backed configuration persistence

## Project layout

- `picow/` — Pico W firmware implementation
- `dependences/libmobile/` — local copy of the libmobile protocol implementation
- `doc/` — project documentation
- `PicoAdapter_PCB/` — board and schematic files

## Recommended hardware

- Raspberry Pi Pico W or Pico 2 W
- Bidirectional level shifter for Game Boy link-cable signals
- Game Boy link cable
- 5V power source capable of powering the adapter

## Firmware overview

The current recommended firmware target is the Pico W implementation under `picow/`.

On boot, the device:

1. initializes the Pico peripherals
2. loads saved configuration from EEPROM
3. connects to Wi-Fi using the stored SSID/password
4. starts the web setup interface if Wi-Fi is unavailable or not yet configured
5. waits for Game Boy communication
6. stops the web server as soon as the Game Boy begins talking

## Supported options

The build is configured through CMake options:

- `PICO_BOARD` — `pico_w` or `pico2_w`
- `ADAPTER` — `REON` or `STACKSMASHING`

These are defined in the top-level `CMakeLists.txt`.

## Build instructions

See [doc/BUILDING.md](BUILDING.md) for the exact build flow and command examples.

## Configuration

See [doc/CONFIGURATION.md](CONFIGURATION.md) for the current web configuration flow, Wi-Fi setup, and troubleshooting notes.

## Legacy / fallback notes

Older documentation in the wiki described a serial-based setup flow. The current firmware is primarily configured through the built-in web interface. The serial menu remains a useful fallback for troubleshooting, but the recommended path is the web setup UI.
