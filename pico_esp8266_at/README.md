# Hardware Setup

## Esp8266 ESP-01
You need flash your ESP with the [Latest NonOS Firmware](https://github.com/espressif/ESP8266_NONOS_SDK/releases/latest/) and download the [Latest Flash Download Tool](https://www.espressif.com/en/support/download/other-tools) from espressif.

You will need an USB adapter to flash the firmware and solder a pushbutton on GPIO 0 and GND on the adapter. Connect the adapter with the button pressed and release after 2 seconds after connected, this will put the ESP into "programmable mode" to flash the firmware.

When you open the Flash Download Tool, choose ESP8266 and Develop Mode. Once the program open, set the options as shown below:
* uncheck DoNotChgBin
* SPI speed 40Mhz
* SPI mode QIO
* BAUD 115200

On the top list, choose this files and set to this address:

* 0x00000 - bin\boot_v1.7.bin
* 0x01000 - bin\at\512+512\user1.1024.new.2.bin
* 0xFC000 - bin\esp_init_data_default_v08.bin
* 0x7E000 - bin\blank.bin
* 0xFE000 - bin\blank.bin

Click in Start and wait the FINISH message.

Currently it's not supports the ESP-AT firmware (v2.2 for ESP8266 or newer versions for ESP32) and I didn't test other ESP boards. Do at your own risk.

| ESP8266 ESP-01 |  Pico  |
|------------|---------|
| TX         |   GPIO 5   |
| GND        |   GND   |
| CH_PD      |   N/A   |
| GPIO 2     |   N/A   |
| RST        |   N/A   |
| GPIO 0     |   N/A   |
| 3.3V       |   3v3 Out   |
| RX         |   GPIO 4   |

**⚠If this doesn't work, try to flip around GPIO 4 and GPIO 5.⚠**

## Link Cable
```
 __________
|  6  4  2 |
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
