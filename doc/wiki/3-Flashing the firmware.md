First of all, get the latest release for your setup (the UF2 file)

* Stable Release [![Release Version](https://img.shields.io/github/v/release/zenaror/PicoAdapterGB?style=plastic)](https://github.com/zenaror/PicoAdapterGB/releases/latest/)
* Beta Release [![Beta Version](https://img.shields.io/github/v/release/zenaror/PicoAdapterGB?include_prereleases&sort=date&filter=beta-*)](https://github.com/zenaror/PicoAdapterGB/releases?q=beta-*)
* Bleeding Edge [![Bleeding Edge](https://img.shields.io/github/actions/workflow/status/zenaror/PicoAdapterGB/bleeding-edge.yml?branch=develop&style=plastic&label=Bleeding%20Edge)](https://github.com/zenaror/PicoAdapterGB/releases#release-bleeding-edge)

After that, connect your Pico to your computer holding the **BOOTSEL** button (or just **BOOT** in some generic boards). This should make your Pico recognized as a Storage Device on your computer called "RPI-RP2".

Now, just copy the UF2 file to the root of this device. The Pico should reset automatically after finish the copy and start to run the program automatically. The LED turns on solid as soon as it boots; if it stays solid and then turns off, it connected normally or started its own setup hotspot. If it blinks a few times first, that's an error code (see [Configuring the device](https://github.com/zenaror/PicoAdapterGB/blob/main/doc/wiki/4-Configuring%20the%20device.md) for what each blink count means) before it falls back to the setup hotspot.

