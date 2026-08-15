# Configuring the device

The recommended configuration method for the Pico W build is the built-in web setup interface.

## Web configuration flow

On power-up, the adapter tries to connect to the saved Wi-Fi network using the stored SSID and password.

If the Wi-Fi connection succeeds, the adapter starts normal operation. If no valid Wi-Fi configuration is available, the device starts its fallback access point:

- SSID: `PicoAdapterGB`
- Password: `magb!123`

Then the configuration page is available at the fixed address:

```text
http://192.168.4.1/
```

Important: the fallback hotspot currently does not provide DHCP service. When connecting to it, the client must be configured manually with a compatible IP in the same subnet, typically something like `192.168.4.2`, and then the browser should open `http://192.168.4.1/`.

From there you can configure:

- Wi-Fi SSID
- Wi-Fi password
- DNS 1 / DNS 2
- DNS port
- relay server
- relay token
- P2P port
- adapter device type
- unmetered mode
- redirect mail option

The form includes a Save & Reboot action to persist the values and restart the device.

## What happens after Game Boy communication starts

The web server is intentionally shut down as soon as the Game Boy begins talking to the adapter. This is a deliberate behavior: the setup page is meant to be available only before normal operation starts.

This is the current behavior of the firmware used in the `picow` target.

## Serial fallback

The serial console is no longer the normal configuration path for the device.

It is kept only as a troubleshooting fallback for:

- boot and startup issues
- Wi-Fi recovery
- checking logs and state during debugging
- confirming the Pico is initializing correctly

This is not the recommended way to configure the device in normal use.

## Important notes

- The main configuration flow is now the web interface.
- The web interface is the recommended method for user-facing setup.
- Serial remains only as a debugging tool, not a primary configuration method.

## P2P / relays note

In P2P communication, the game will only work correctly when both players use matching adapter settings, especially for the `unmetered` option. This is a known behavior of the original Mobile Adapter GB design and not a limitation specific to the web UI itself.

Example:

| Player 1 | Player 2 | Result |
|----------|----------|--------|
| Blue Adapter with Unmetered = 1 | Blue Adapter with Unmetered = 1 | Works |
| Blue Adapter with Unmetered = 0 | Blue Adapter with Unmetered = 0 | Works |
| Blue Adapter with Unmetered = 1 | Blue Adapter with Unmetered = 0 | Does not work |
| Red Adapter with Unmetered = 1 | Red Adapter with Unmetered = 1 | Works |