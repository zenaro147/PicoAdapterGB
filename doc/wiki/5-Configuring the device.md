### Known Issues

Make sure your Serial Monitor/Terminal is able to send the proper line ending "CRLF" or "\r\n". https://github.com/zenaro147/PicoAdapterGB/issues/62

--------------------------

To configure your WiFi network and some other parameters, you need to put the device in "Config Mode". To do that, connect the device to a PC and, using a Serial Monitor (like Putty, TeraTerm, Hercules, or even your Android Smartphone using an OTG cable and Serial USB Terminal app) at Baud rate 115200, send any character when the text `Press any key to enter in Setup Mode...` shows up.

The message `Enter a command:` appears, you can enter the following commands below to set a value. To clear/reset the parameter, just enter the command without any value next, like this `WIFISSID=`.

The commands syntax is: `<COMMAND>=<VALUE>`, for example `WIFISSID=MY_WIFI`.

All commands are Case-Sensitive.

|   Command   | Effect |
|-------------|-------------|
| WIFISSID      | Set the SSID to use to connect. |
| WIFIPASS      | Set the password from the WiFi network to use to connect. |
| DNS1          | Set primary DNS that the adapter will use to parse the nameservers. |
| DNS2          | Set secondary DNS that the adapter will use to parse the nameservers. | 
| DNSPORT       | Set a custom DNS port to use with the DNS servers. |
| RELAYSERVER   | Set a Relay Server that will be use during P2P communications. |
| RELAYTOKEN    | Set a Relay Token that will be used on Relay Server to receive a valid number to use during P2P communications. |
| P2PPORT       | Set a custom P2P port to use during P2P communications (Local Network only). |
| DEVICE        | Set the Device to emulate (BLUE, RED or YELLOW). |
| UNMETERED     | Set if the device will be Unmetered (useful for Pokemon Crystal). Only accept 1 (true) or 0 (false). |

Special commands (just enter the command, without =<VALUE>):
|    Command    | Effect |
|---------------|-------------|
| FORMAT_EEPROM | Format the eeprom, if necessary. |
| SHOW_CONFIG   | Show the actual device configuration. |
| EXIT          | Quit from Config Mode and Save the new values. If you change some value, the device will reboot. |
| HELP          | Show this command list on screen. |

--------------------------

PS: In P2P communications (aka "calling another player" as happens in Pokemon Crystal to Trade/Battle), the game will only work if both players have the same Adapter with the same "unmetered" settings. This is not a limitation of the code itself or the server, but rather how the original adapter was made to work.

For example:
| Player 1 | Player 2 | Effect |
|----------|----------|--------|
|Blue Adapter with Unmetered = 1 | Blue Adapter with Unmetered = 1 | WORK |
|Blue Adapter with Unmetered = 0 | Blue Adapter with Unmetered = 0 | WORK |
|Blue Adapter with Unmetered = 1 | Blue Adapter with Unmetered = 0 | NOT WORK |
|Red Adapter with Unmetered = 1 | Blue Adapter with Unmetered = 1 | NOT WORK |
|Red Adapter with Unmetered = 0 | Blue Adapter with Unmetered = 0 | NOT WORK |
|Red Adapter with Unmetered = 1 | Blue Adapter with Unmetered = 0 | NOT WORK |
|Red Adapter with Unmetered = 1 | Red Adapter with Unmetered = 1 | WORK |