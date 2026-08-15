First of all, get the latest release for your setup (the UF2 file)

Latest Stable Release  [![Release Version](https://img.shields.io/github/v/release/zenaro147/PicoAdapterGB?style=plastic)](https://github.com/zenaro147/PicoAdapterGB/releases/latest/)  [![Release Date](https://img.shields.io/github/release-date/zenaro147/PicoAdapterGB?style=plastic)](https://github.com/zenaro147/PicoAdapterGB/releases/latest/)
<br>Latest Development Release  [![Release Version](https://img.shields.io/github/release/zenaro147/PicoAdapterGB/all.svg?style=plastic)](https://github.com/zenaro147/PicoAdapterGB/releases/) [![Release Date](https://img.shields.io/github/release-date-pre/zenaro147/PicoAdapterGB.svg?style=plastic)](https://github.com/zenaro147/PicoAdapterGB/releases/) 

After that, connect your Pico to your computer holding the **BOOTSEL** button (or just **BOOT** in some generic boards). This should make your Pico recognized as a Storage Device on your computer called "RPI-RP2".

Now, just copy the UF2 file to the root of this device. The Pico should reset automatically after finish the copy and start to run the program automatically. Just wait until the LED starts to blink, this indicate that the default configuration was applied and the device didn't connect to the internet.