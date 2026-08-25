# Building PicoAdapterGB

This project is built with CMake and the Raspberry Pi Pico SDK.

## Requirements

- Raspberry Pi Pico SDK
- CMake
- Ninja or Make
- ARM GCC toolchain
- Pico W or Pico 2 W board support (required by the `picow` implementation), or a
  plain Pico/Pico 2 plus an ESP8266EX (ESP-01) running ESP-AT (required by the
  `esp` implementation - see [src/implementations/esp/README.md](../src/implementations/esp/README.md))

## Three independent build dimensions

| Variable | Selects | Current values |
|---|---|---|
| `PICO_BOARD` | the RP2040/RP2350 chip/board (Pico SDK's own variable) | `pico_w`, `pico2_w` (picow); `pico`, `pico2` (esp) |
| `PICOADAPTER_IMPLEMENTATION` | which connectivity/hardware backend under `src/implementations/` | `picow` (default), `esp` |
| `ADAPTER` | the Game Boy link-cable pinout | `REON` (default), `STACKSMASHING` |

These are independent: the board says what chip you have, the implementation says what connects it to the network, and the adapter says which link-cable pinout you built. `PICOADAPTER_IMPLEMENTATION=picow` requires a board with cyw43 Wi-Fi support (`pico_w`/`pico2_w`); `PICOADAPTER_IMPLEMENTATION=esp` requires a plain board with no onboard Wi-Fi (`pico`/`pico2` - the ESP8266EX is external, wired over UART). Selecting an implementation with an incompatible board fails the configure step with a clear error instead of silently building something else, and an unrecognized `PICO_BOARD` value fails the same way rather than falling back to a different board's firmware.

## Configure the build

From the project root:

```bash
cmake -S . -B build \
  -DPICO_BOARD=pico_w \
  -DADAPTER=REON \
  -DCMAKE_BUILD_TYPE=Release
```

(`PICOADAPTER_IMPLEMENTATION` defaults to `picow`, so it doesn't need to be passed explicitly today.)

The firmware version shown in boot logs and the web interface defaults to
`1.5.6-beta`. It can be overridden at configure time:

```bash
cmake -S . -B build \
  -DPICO_BOARD=pico_w \
  -DADAPTER=REON \
  '-DPICO_ADAPTER_SOFTWARE=custom-build-name'
```

The bleeding-edge workflow sets this automatically to `bleeding-edge` plus
the full commit SHA used for the build.

Common variants:

```bash
# Pico W + REON pinout
cmake -S . -B build -DPICO_BOARD=pico_w -DADAPTER=REON

# Pico W + Stack smashing pinout
cmake -S . -B build -DPICO_BOARD=pico_w -DADAPTER=STACKSMASHING

# Pico 2 W + REON pinout
cmake -S . -B build -DPICO_BOARD=pico2_w -DADAPTER=REON

# Pico 2 W + Stack smashing pinout
cmake -S . -B build -DPICO_BOARD=pico2_w -DADAPTER=STACKSMASHING

# Explicit implementation (currently a no-op for picow, since it's the default)
cmake -S . -B build -DPICO_BOARD=pico_w -DADAPTER=REON -DPICOADAPTER_IMPLEMENTATION=picow

# Pico + ESP8266EX (ESP-01) + REON pinout - see src/implementations/esp/README.md
cmake -S . -B build -DPICO_BOARD=pico -DPICOADAPTER_IMPLEMENTATION=esp -DADAPTER=REON

# Pico 2 + ESP8266EX (ESP-01) + Stack smashing pinout
cmake -S . -B build -DPICO_BOARD=pico2 -DPICOADAPTER_IMPLEMENTATION=esp -DADAPTER=STACKSMASHING
```

## Build

Each combination of board/implementation/pinout produces its own uniquely-named CMake target. The configure step prints it:

```text
-- PicoAdapterGB target: PicoAdapterGB_PicoW_REON
```

Build that exact target:

```bash
cmake --build build --target PicoAdapterGB_PicoW_REON
```

or just build everything configured in that build directory:

```bash
cmake --build build
```

## Output location and artifact names

Artifacts are named `PicoAdapterGB_<Board><ImplementationSuffix>_<Pinout>`, written to `build/release/<Pinout>/<Board><ImplementationSuffix>/`:

| Board | Implementation | Pinout | Target / artifact base name | Output directory |
|---|---|---|---|---|
| `pico_w` | `picow` | `REON` | `PicoAdapterGB_PicoW_REON` | `build/release/REON/PicoW/` |
| `pico_w` | `picow` | `STACKSMASHING` | `PicoAdapterGB_PicoW_SmBoard` | `build/release/SmBoard/PicoW/` |
| `pico2_w` | `picow` | `REON` | `PicoAdapterGB_Pico2W_REON` | `build/release/REON/Pico2W/` |
| `pico2_w` | `picow` | `STACKSMASHING` | `PicoAdapterGB_Pico2W_SmBoard` | `build/release/SmBoard/Pico2W/` |
| `pico` | `esp` | `REON` | `PicoAdapterGB_PicoESP_REON` | `build/release/REON/PicoESP/` |
| `pico` | `esp` | `STACKSMASHING` | `PicoAdapterGB_PicoESP_SmBoard` | `build/release/SmBoard/PicoESP/` |
| `pico2` | `esp` | `REON` | `PicoAdapterGB_Pico2ESP_REON` | `build/release/REON/Pico2ESP/` |
| `pico2` | `esp` | `STACKSMASHING` | `PicoAdapterGB_Pico2ESP_SmBoard` | `build/release/SmBoard/Pico2ESP/` |

The `picow` implementation contributes no extra suffix to the hardware name, since `pico_w`/`pico2_w` already imply Wi-Fi connectivity. The `esp` implementation runs on a plain (non-`_w`) board that says nothing about connectivity on its own, so it appends its own `ESP` suffix instead (see [ARCHITECTURE.md](ARCHITECTURE.md)).

Each directory contains the usual `.uf2`/`.elf`/`.bin`/`.hex`/`.map` outputs from `pico_add_extra_outputs()`.

## Notes

- `PICO_BOARD` must be one of `pico_w`/`pico2_w` (for `picow`) or `pico`/`pico2` (for `esp`). An unrecognized value, or a board incompatible with the selected implementation, fails the configure step immediately - it never silently falls back to a different board's firmware.
- `PICOADAPTER_IMPLEMENTATION` must be `picow` or `esp`. An unrecognized value fails the configure step immediately.
- `ADAPTER` must be one of `REON` or `STACKSMASHING`. If it's not recognized, the project falls back to `REON`.
- The configure step prints a summary (`Board:`, `Implementation:`, `Pinout:`) so you can confirm what's about to build.

## Flashing

After a successful build, flash the generated `.uf2` file using the Pico bootloader mode or your preferred flashing method.

For example, after the UF2 file is generated, drag and drop it onto the Pico when it appears as a USB mass-storage device.
