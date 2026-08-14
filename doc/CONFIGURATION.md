# Configuration

The current configuration flow is primarily web-based.

## Normal startup flow

On boot, the Pico tries to connect to the saved Wi-Fi network using the stored SSID and password.

If the Wi-Fi connection succeeds, the adapter will continue normally and the configuration page is available only while the device is still in setup mode.

If the Wi-Fi connection is not available or the saved network is not configured yet, the Pico starts its own access point:

- SSID: `PicoAdapterGB`
- password: `magb!123`

Then the setup page is available at:

```text
http://192.168.4.1/
```

Important: the fallback hotspot does not currently provide DHCP service. When connecting to it, the client must be configured manually with a compatible IP in the same subnet, typically something like `192.168.4.2`, and then the browser should open `http://192.168.4.1/`.

## Web setup page

The built-in web interface lets you configure:

- Wi-Fi SSID
- Wi-Fi password
- primary and secondary DNS
- DNS port
- relay server
- relay token
- P2P port
- adapter device type
- unmetered mode
- redirect mail behavior

The page is designed to be used from a laptop, phone, or tablet connected to the adapter's Wi-Fi network.

## Web shutdown behavior

The configuration server is intentionally stopped as soon as the Game Boy starts communication. This prevents the setup page from staying active while the adapter is being used in normal operation.

The firmware launches a small watchdog task on the second core that listens for the first Game Boy traffic and then triggers the web server shutdown.

## Save and reboot

The configuration form includes a single action called `Save & Reboot`.

When you save the configuration, the firmware persists the settings and reboots to apply them.

## Serial fallback

The serial console is no longer the normal configuration path for the device.

It is kept only as a troubleshooting and debugging fallback for cases such as:

- startup issues
- Wi-Fi recovery
- checking boot logs
- verifying that the adapter is initializing correctly

This is not the recommended way to configure the device in normal operation.

## Troubleshooting

### Web page does not appear

- Confirm the Pico is in access-point mode after boot.
- Check the Wi-Fi connection status on the serial console.
- Connect to `PicoAdapterGB` using the default password `magb!123`.
- Open the page at `http://192.168.4.1/`.

### Game Boy communication stops the web server too late

- Ensure the Game Boy link cable is wired correctly.
- Verify the level shifter and the serial pin mapping.
- Check the serial console for connection/log messages.

### Wi-Fi is not connecting

- Verify the SSID and password were saved correctly.
- Confirm the network is reachable and not blocked by AP isolation or captive portal rules.
- Reset the adapter and open the setup page again.

## Notes

This project is a compatibility-focused firmware. Small hardware and timing details matter, especially on the Game Boy link-cable side. If setup is failing, the first place to check is the physical wiring and the timing-sensitive Game Boy connection.
