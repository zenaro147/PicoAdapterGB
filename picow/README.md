# Hardware Setup

### Link Cable setup
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


# Configuring the device
To configure your WiFi network and some other parameters, you need to put the device in "Config Mode". To do that, connect the device to a PC and, using a Serial Monitor, send any character when the text `Press any key to enter in Setup Mode...` shows.

When the message `Enter a command:` appears, you can enter the following commands below to set a value. To clear/reset the parameter, just enter the command without any value next, like this `WIFISSID=`.

The commands syntax is: `<COMMAND>=<VALUE>`, for example `WIFISSID=MY_WIFI`.

All commands are Case-Sensitive.

|   Command   | Effect |
|-------------|-------------|
| WIFISSID    | Set the SSID to use to connect. |
| WIFIPASS    | Set the password from the WiFi network to use to connect. |
| DNS1        | Set primary DNS that the adapter will use to parse the nameservers. |
| DNS2        | Set secondary DNS that the adapter will use to parse the nameservers. | 
| DNSPORT     | Set a custom DNS port to use with the DNS servers. |
| RELAYSERVER | Set a Relay Server that will be use during P2P communications. |
| RELAYTOKEN  | Set a Relay Token that will be used on Relay Server to receive a valid number to use during P2P communications. |
| P2PPORT     | Set a custom P2P port to use during P2P communications (Local Network only). |
| UNMETERED   | Set if the device will be Unmetered (useful for Pokemon Crystal). |
| EXIT        | Quit from Config Mode and Save the new values. If you change some value, the device will reboot. |
| HELP        | Show this command list on screen. |
