* During the usage, sometimes the LED should turn on. This indicates that the device need to save a new configuration (this could during the Mobile Trainer first configuration, for example). If this happens, **LET THE LED TURNS OFF BEFORE DO ANY OTHER ACTION IN GAME!**
<br>This is a Pico limitation, since it doesn't have a real EEPROM, like Arduino boards. So the firmware basically use a small writable part of the flash memory to save these configurations.
<br>This memory is kind of sensible if any interrupt runs during this action and can corrupt your configuration, being necessary to format this flash memory chunck and reconfigure again using the config mode (See for [Configuring the device](https://github.com/zenaro147/PicoAdapterGB/wiki/Configuring-the-device) more details)

---------------------------------

* In P2P communications (aka "calling another player" as happens in Pokemon Crystal to Trade/Battle), the game will only work if both players have the same Adapter with the same "unmetered" settings. This is not a limitation of the code itself or the server, but rather how the original adapter was made to work.

For example:

| Player 1 | Player 2 | Effect |
| ---------- | ---------- | -------- |
| Blue Adapter with Unmetered = 1 | Blue Adapter with Unmetered = 1 | WORK |
| Blue Adapter with Unmetered = 0 | Blue Adapter with Unmetered = 0 | WORK |
| Blue Adapter with Unmetered = 1 | Blue Adapter with Unmetered = 0 | NOT WORK |
| Red Adapter with Unmetered = 1 | Blue Adapter with Unmetered = 1 | NOT WORK |
| Red Adapter with Unmetered = 0 | Blue Adapter with Unmetered = 0 | NOT WORK |
| Red Adapter with Unmetered = 1 | Blue Adapter with Unmetered = 0 | NOT WORK |
| Red Adapter with Unmetered = 1 | Red Adapter with Unmetered = 1 | WORK |

---------------------------------
