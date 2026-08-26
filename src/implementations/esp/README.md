# esp implementation

A `net_hal.h`/`socket_hal.h`/`led_hal.h` backend for a plain Raspberry Pi
Pico / Pico 2 (no onboard Wi-Fi) paired with an external **ESP8266EX**
module - specifically an **ESP-01** breakout - running Espressif's official
**ESP-AT v2.3.0.0** firmware, talked to over UART. Selected with
`-DPICOADAPTER_IMPLEMENTATION=esp`.

This is the second implementation of the interfaces `picow`
(`src/implementations/picow/`) already implements for Pico W / Pico 2 W's
onboard CYW43. See [doc/ARCHITECTURE.md](../../doc/ARCHITECTURE.md) for how
the two fit into the overall core/implementation split.

## Hardware

```text
Raspberry Pi Pico / Pico 2 (PICO_BOARD=pico / pico2)
        +
ESP8266EX / ESP-01, running ESP-AT v2.3.0.0
```

Not supported by this implementation: Pico W, Pico 2 W (use `picow`
instead - a board with onboard Wi-Fi has no use for an external ESP8266),
ESP32/ESP32-S2/ESP32-C3, or ESP-AT versions other than the ESP8266 2.3.0.0
line. Behavior specific to those was deliberately not assumed anywhere in
this backend - see "Known limitations" below.

### Wiring

| ESP8266EX / ESP-01 | Raspberry Pi Pico           | Function               |
| ------------------- | ---------------------------- | ---------------------- |
| `RX`                 | `GP4` — physical pin 6        | Pico UART1 TX → ESP RX |
| `TX`                 | `GP5` — physical pin 7        | ESP TX → Pico UART1 RX |
| `GND`                | `GND` — physical pin 8        | Common ground           |
| `VCC`                | `3V3(OUT)` — physical pin 36  | 3.3V supply              |

Only these four lines. There is no connection to `RESET`, `GPIO0`, `GPIO2`,
or `EN`/`CH_PD` - this backend cannot physically reset the ESP module, force
it into bootloader mode, or reflash its firmware. It can only talk AT
commands to whatever is already running when the Pico boots (see "Recovery"
below).

GP4/GP5 select `UART1`'s primary function on **both** RP2040 and RP2350 -
confirmed directly from the Pico SDK's own GPIO function tables
(`hardware/gpio.h`), not assumed from a datasheet read from memory (see
`net/esp_config.h`'s comment for the exact source). The board's default
debug UART (`uart0`, GP0/GP1) is untouched, so USB/UART stdio logging works
exactly as it does on `picow`.

## ESP-AT firmware

This backend does **not** flash or manage the ESP8266's firmware (see
`PicoAdapterGB-Claude.md`'s task instructions - out of scope by design). The
module must already be running:

```text
ESP-AT v2.3.0.0 for ESP8266
```

Reference: https://docs.espressif.com/projects/esp-at/en/release-v2.3.0.0_esp8266/

Every AT command this backend sends was checked against that specific
version's documentation. Where the documentation didn't confirm the exact
wire format (see "Known limitations"), the parser was written to tolerate a
plausible variation rather than assume one.

### Baud rate

**115200 8N1** - ESP-AT's factory default. This backend never sends
`AT+UART_CUR`/`AT+UART_DEF` to change it: reliability took priority over
throughput, and reconfiguring the module's own UART adds a failure mode (a
misconfigured/already-changed module becoming unreachable) for no benefit
this project needs. If a module was previously reconfigured to a different
baud rate, restore it first (`AT+UART_DEF=115200,8,1,0,0` from a terminal,
or `AT+RESTORE`).

### Initialization sequence (`esp_at_init()`, see `net/esp_at.c`)

1. Repeated `AT` probe (up to `ESP_AT_SYNC_ATTEMPTS`, see `net/esp_config.h`) - there's no reset line, so this is the only way to find out the module is alive and to absorb whatever it prints on its own power-on.
2. `ATE0` - disable command echo, so the parser never has to distinguish an echoed command from a real response line.
3. `AT+CWMODE=3` - station+AP mode: station for normal operation, AP for the first-boot setup hotspot (`net_wifi_start_ap()`).
4. `AT+CIPMUX=1` - multi-connection mode; this backend always needs at least 2 (mobile) + up to 2 (web) simultaneous link IDs.
5. `AT+CIPRECVMODE=1` - passive receive (see "Receive model" below).
6. `AT+CIPDINFO=0` - don't prefix `+IPD`/`+CIPRECVDATA` with the remote address; this backend doesn't rely on it (see `socket_impl_recv()`'s UDP handling).

### Boot failure: module not responding

`esp_at_init()` (and therefore `net_init()`, see `net/esp_net.c`) returns `false` if the module never answers the `AT` probe after `ESP_AT_SYNC_ATTEMPTS` retries, or if any command in the sequence above fails. `main.c` treats a `false` return from `net_init()` as fatal: it halts boot right there, blinking LED error code 4 (`LED_ERROR_NET_INIT_FAILED`, see `core/led_status.h`) forever instead of falling through into a Wi-Fi connect attempt that has no chance of succeeding (see `doc/CONFIGURATION.md`'s LED status section).

This is deliberately a hard halt, not a fallback to the setup hotspot: the hotspot needs working network hardware too, so there's nothing safe to fall back to when the module itself isn't answering. Check wiring (GP4/GP5, see "Wiring" above), power, and that the module is actually running ESP-AT firmware at `ESP_UART_BAUD_RATE` (115200) before assuming this is a software bug.

### Full list of AT commands used

```text
Basic:       AT, ATE0
Wi-Fi:       AT+CWMODE, AT+CWJAP, AT+CWSAP, AT+CIFSR
TCP/IP:      AT+CIPMUX, AT+CIPRECVMODE, AT+CIPDINFO, AT+CIPSTART,
             AT+CIPSEND, AT+CIPRECVDATA, AT+CIPCLOSE,
             AT+CIPSERVER, AT+CIPSERVERMAXCONN
```

Nothing else - no `AT+CIPSTATUS` polling (link state is tracked from
unsolicited events, see below), no TLS/SSL (`AT+CIP...SSL`), no MQTT/HTTP-AT,
no Bluetooth commands.

## Receive model: passive (`CIPRECVMODE=1`), not active `+IPD` push

ESP-AT offers two receive models. This backend uses **passive**
(`AT+CIPRECVMODE=1` + `AT+CIPRECVDATA`), not the default **active** model
(unsolicited `+IPD,<len>:<data>` pushes). Reasoning (see `net/esp_at.h`'s
file header comment for the implementation-level detail):

- **Binary-safety.** `AT+CIPRECVDATA`'s response (`+CIPRECVDATA:<len>,<data>`)
  is the only place raw socket payload ever appears on the wire, and it's
  always framed by a command *we* issued and a byte count *we* already know
  before the payload starts. Active `+IPD` pushes mix arbitrary binary
  payload into the same asynchronous stream as `\r\n`-terminated protocol
  lines, which is fundamentally harder to parse correctly when the payload
  itself can contain `\r`, `\n`, or text that looks like AT syntax (see
  `PicoAdapterGB-Claude.md`'s ESP-AT task spec, section 10/22).
- **Pull, don't push.** `net_poll()`/`mobile_loop()` control exactly when a
  chunk is read (up to `MOBILE_MAX_TRANSFER_SIZE` = 254 bytes at a time, see
  `socket_impl_recv()`), instead of the module deciding when to hand us
  however much just arrived.
- The `+IPD,<id>,<len>` line passive mode still emits is short, plain ASCII,
  and only ever a *length hint* (see `esp_at.c`'s `links[].rx_pending`) - the
  actual bytes only move over the wire inside a `CIPRECVDATA` response we
  requested.
- **`AT+CIPRECVDATA` can fail with a plain `ERROR` instead of "0 bytes, OK"
  when the remote closes the connection right around the time it's
  requested** - confirmed on hardware against a real non-keep-alive HTTP
  server closing right after its last bytes. `socket_impl_recv()` treats
  this the same as the normal "remote closed" signal (`-2`, see `mobile.h`'s
  `sock_recv` doc) whenever the link is no longer connected by the time the
  error comes back, instead of surfacing it as a hard transport failure -
  otherwise this showed up as a spurious "TCP connection fail" in-game for
  what was actually a complete, successful page load.

## Link IDs and the shared server slot

`AT+CIPMUX=1` gives 5 link IDs (0-4). The 2 libmobile connections
(`MOBILE_MAX_CONNECTIONS`) get fixed IDs 0 and 1, reserved once at boot
(`socket_hal_bind()`); the web config server's connections get whatever ID
`AT+CIPSERVER`'s auto-accept assigns from what's left.

**The ESP8266 only supports one `AT+CIPSERVER` at a time** ("Only one server
can be created at most" - ESP-AT TCP/IP AT command doc). This backend shares
a single server slot between the web setup UI (port 80) and libmobile's own
TCP listen/accept (`socket_impl_listen`/`_accept`, used for Mobile Adapter
P2P), arbitrated in `net/esp_at.c`'s `esp_at_server_start/_stop/_owner()`.
This is safe **because** the existing boot sequence (`src/main.c`) already
tears the web server down ~1 second after a Game Boy session starts, and
libmobile only ever calls `socket_impl_listen()` from within an active
session - i.e. the two uses are already temporally disjoint in this
project's own design, for an unrelated reason (not wanting the web UI
competing with an active Game Boy session). This backend's `socket_impl_listen()`
simply fails (returns `false`) if the web server still happens to hold the
slot; it does not forcibly evict it.

## Known limitations / not hardware-validated

Everything below has not been exercised against a real ESP8266EX - only
build-verified (see "Builds" below). Flagged explicitly rather than silently
assumed correct:

- **IPv6 is not implemented.** `socket_impl_open()` fails for
  `MOBILE_ADDRTYPE_IPV6`, which `mobile.h`'s own contract explicitly permits
  ("IPV6 support is optional, and the implementation is allowed to fail with
  that"). ESP8266 ESP-AT 2.3.0.0's own IPv6 support is partial/version-
  dependent and unused by this project.
- **Unsolicited event text.** `"WIFI CONNECTED"` and `"WIFI GOT IP"` were
  confirmed verbatim from the ESP-AT Wi-Fi AT command documentation's own
  example transcript. `"WIFI DISCONNECT"` and the `"<id>,CONNECT"`/
  `"<id>,CLOSED"` link-event lines are long-standing, widely-used ESP-AT
  conventions, but the specific documentation pages fetched while building
  this backend did not include a verbatim example transcript containing
  them. The parser (`net/esp_at.c`'s `process_line()`) matches these by
  fixed prefix rather than depending on exact byte-for-byte formatting, but
  this should be confirmed against a real module's serial output before
  relying on it.
- **Abandoning an in-flight connect/send via close().** libmobile is allowed
  to cancel a `sock_connect`/`sock_send` in progress by calling
  `sock_close()` instead of ever collecting the result (see `mobile.h`).
  `esp_at_close()` reclaims the AT "bus" for this case, but if the abandoned
  command's real response arrives on the wire just as a *new* command is
  being issued, there's a narrow window where a stray response line could be
  misattributed to the new command instead of silently discarded. Confirmed
  on real hardware during relay-connect testing (a stale `SEND OK` from an
  abandoned handshake send showed up while a subsequent `AT+CIPSTART` retry
  was in flight). `esp_at_close()` now drains the UART for a short bounded
  window (`ESP_AT_ABANDON_DRAIN_MS`) after reclaiming the bus, to let such a
  straggler arrive and be discarded before issuing its own command - this
  narrows the window but doesn't close it entirely (the module could still
  reply after that window). See the comment in `net/esp_at.c`'s
  `esp_at_close()`.
- **`esp_at_send()` is bounded-blocking, not non-blocking** (up to
  `ESP_AT_TIMEOUT_SEND_MS` in the worst case), unlike `mobile.h`'s documented
  `sock_send()` contract ("non-blocking... called repeatedly until all of the
  data is sent"). This is a deliberate workaround for a bug found in the
  pinned `dependences/libmobile` submodule: `relay.c`'s `relay_handshake_send()`
  and `dns.c` both do `return mobile_cb_sock_send(...)` from a `bool`-returning
  function, implicitly truncating `sock_send()`'s documented `int` return (0
  is a valid "nothing sent *yet* this call, call again" per the contract) to
  a boolean - a legitimate 0 is misread as failure. This broke every relay
  connection through this backend, because a real `AT+CIPSEND` round-trip
  (`OK`, then the `>` prompt, then `SEND OK`) can never finish on its first
  call, unlike picow's lwIP-backed send, which usually finishes a payload as
  small as the relay handshake synchronously and never triggers the bug. The
  correct fix belongs in the submodule (see `CLAUDE.md`'s submodule rules);
  fixing it there was raised and explicitly deferred in favor of this
  backend-local workaround. Practical effect: not just the relay handshake,
  but *any* `esp_at_send()` call - ordinary Game Boy protocol sends, and web
  config response bytes (`web/web_http.c`) - can block the main loop for up
  to `ESP_AT_TIMEOUT_SEND_MS`.
- **`AT+CIPSERVER` link ID for the web UI is not reserved in advance** (ESP-AT
  auto-assigns it from whatever's free), so a specific-but-unlikely ordering
  where a mobile socket is still open on the ID the server would otherwise
  auto-assign could produce a collision. Not expected in this project's
  normal flow (web server is only up before/around Wi-Fi setup, mobile
  sockets only open during an active Game Boy session), but not proven
  impossible either.
- Timeouts (`net/esp_config.h`) are reasoned estimates (association can
  legitimately take several seconds; `AT+CIPSTART`/`AT+CIPSEND` similarly),
  not measured against real hardware.

## Differences from `picow`

| | `picow` | `esp` |
|---|---|---|
| Wi-Fi hardware | Onboard CYW43 | External ESP8266EX (ESP-01) over UART |
| Board | `pico_w`, `pico2_w` | `pico`, `pico2` |
| Network stack | lwIP (in-Pico) | ESP-AT (on the ESP8266; Pico only speaks AT) |
| Socket callbacks | lwIP callbacks (event-driven) | Polled async state machine (no callback mechanism exists over a UART/AT link) |
| `net_wifi_connect()`/`sock_connect()`/`sock_recv()` blocking behavior | `net_wifi_connect()` blocks, connect/recv are non-blocking | Same shape, implemented against `net/esp_at.h` instead of `cyw43_arch_poll()` |
| `sock_send()` blocking behavior | Non-blocking (matches `mobile.h`'s documented contract) | Bounded-blocking (up to `ESP_AT_TIMEOUT_SEND_MS`) - deliberate workaround for a libmobile submodule bug, see "Known limitations" above |
| LED | `cyw43_arch_gpio_put()` (LED is on the CYW43 radio) | Plain `gpio_put(PICO_DEFAULT_LED_PIN)` |
| Web server transport | lwIP TCP callbacks | Polled against the shared `AT+CIPSERVER` slot (see above) |
| Web server routes/HTML | `web/web_routes_*.c`, `web/web_page.c` | Byte-for-byte copies of picow's (verified to have zero lwIP dependency) - not shared, to keep the two implementations independent (see doc/ARCHITECTURE.md) |
| IPv6 | Supported | Not implemented (see above) |

## Builds

```bash
cmake -S . -B build -DPICO_BOARD=pico  -DPICOADAPTER_IMPLEMENTATION=esp -DADAPTER=REON
cmake -S . -B build -DPICO_BOARD=pico  -DPICOADAPTER_IMPLEMENTATION=esp -DADAPTER=STACKSMASHING
cmake -S . -B build -DPICO_BOARD=pico2 -DPICOADAPTER_IMPLEMENTATION=esp -DADAPTER=REON
cmake -S . -B build -DPICO_BOARD=pico2 -DPICOADAPTER_IMPLEMENTATION=esp -DADAPTER=STACKSMASHING
```

All four build and link successfully (build-verified as part of adding this
implementation), producing `PicoAdapterGB_PicoESP_REON`,
`PicoAdapterGB_PicoESP_SmBoard`, `PicoAdapterGB_Pico2ESP_REON`, and
`PicoAdapterGB_Pico2ESP_SmBoard` respectively - see
[doc/BUILDING.md](../../doc/BUILDING.md). Verified (`arm-none-eabi-nm` on the
resulting `.elf`) not to link any `cyw43`/`lwip` symbol. No hardware
validation has been performed - see "Known limitations" above.
