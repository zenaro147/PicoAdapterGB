# Building PicoAdapterGB

This project is built with CMake and the Raspberry Pi Pico SDK.

## Requirements

- Raspberry Pi Pico SDK
- CMake
- Ninja or Make
- ARM GCC toolchain
- Pico W or Pico 2 W board support

## Configure the build

From the project root:

```bash
cmake -S . -B build \
  -DPICO_BOARD=pico_w \
  -DADAPTER=REON \
  -DCMAKE_BUILD_TYPE=Release
```

Common variants:

```bash
# Pico W + REON pinout
cmake -S . -B build -DPICO_BOARD=pico_w -DADAPTER=REON

# Pico W + Stack smashing pinout
cmake -S . -B build -DPICO_BOARD=pico_w -DADAPTER=STACKSMASHING

# Pico 2 W + REON pinout
cmake -S . -B build -DPICO_BOARD=pico2_w -DADAPTER=REON
```

## Build

```bash
cmake --build build
```

## Output location

The project writes generated artifacts to the build tree and, for adapter-specific builds, to directories like:

- `build/release/REON/PicoW/`
- `build/release/SmBoard/PicoW/`

The exact output path depends on the selected board and adapter option.

## Notes

- `PICO_BOARD` must be one of `pico_w` or `pico2_w`.
- `ADAPTER` must be one of `REON` or `STACKSMASHING`.
- The firmware targets the Pico W implementation under `picow/` when the board supports the cyw43 Wi-Fi stack.
- If `PICO_BOARD` is not recognized, the project falls back to `pico_w` automatically.

## Flashing

After a successful build, flash the generated `.uf2` file using the Pico bootloader mode or your preferred flashing method.

For example, after the UF2 file is generated, drag and drop it onto the Pico when it appears as a USB mass-storage device.
