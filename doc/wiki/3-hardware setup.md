Different from Arduino that operates in 5V like the GameBoy, the Pico operates in 3.3V on its pins by default. You will need a [tiny bidirectionnal Level Shifter like this](https://www.aliexpress.com/item/1005007531580646.html) to handle the communication protocol and prevent any overvoltage/undervoltage from any side. Direct connection between Game Boy and Pico pins without level shifter may work (especially for GBA games, that works in 3.3v) but we don't recommend this for long term reliability reasons.

Connect the Game Boy serial pins to the Pico pins following this scheme:

![Link Cable view](https://github.com/zenaro147/NeoGB-Printer/blob/master/Supplementary_images/LinkCable.jpg)
```
 ___________
|  6  4  2  |
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
|            |      HV     |   N/A   | I recommend to let this pin unconnected. Also, it's possible to use P1 from Link Cable, but DO AT YOUR OWN RISK (some aftermarket cables have P1 and P4 connected each other. If this is your case, use P4 instead) |

![Schematic](https://raw.githubusercontent.com/zenaro147/PicoAdapterGB/master/doc/PicoWSetup.JPG)<br>
<sub>Pico and Pico W have the same pinout schema</sub>
<br><br>
**⚠If this doesn't work, try to flip around Pin2(Serial Out) and Pin3(Serial In), as the pinout markings of your link cable breakout might be the other way around.⚠**
