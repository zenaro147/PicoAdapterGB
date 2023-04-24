# Hardware Setup

### Link Cable
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
