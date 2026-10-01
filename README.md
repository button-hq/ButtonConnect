# ButtonConnect

The **BYOD device SDK** for the [Button](https://buttonhq.io) cloud. Connect your own
ESP32 / ESP8266 hardware to Button over **MQTT on a TLS WebSocket** on the
`byod/{deviceId}/*` topics — publish button gestures and telemetry, receive commands,
and get press acknowledgements back.

- **WiFi + MQTT over a TLS WebSocket**, with the server certificate always validated
  against two bundled public root CAs.
- **Gestures**: single / double / triple click and click+long chords map straight
  to the triggers you configure in the dashboard.
- **Commands**: built-in `reboot`; actuators handle their own methods via `onCommand`.
- **Cloud-only**: the device needs WiFi and internet access to reach Button; there is no
  local mode. `loop()` advances the connection one step per call, but freezes during each
  TLS handshake — see the note below.

> This is the open-source connection layer extracted from Button's first-party firmware.

## Install

**PlatformIO** (`platformio.ini`) — the registry package pulls in its dependencies:
```ini
lib_deps =
    button-hq/ButtonConnect @ ^0.4.3
platform = espressif32   ; or espressif8266
board = esp32dev         ; XIAO C6 button → seeed_xiao_esp32c6
framework = arduino
```

**Arduino IDE**: until ButtonConnect appears in the Library Manager, download the repo
as a ZIP (`https://github.com/button-hq/ButtonConnect` → Code → Download ZIP), then
Sketch → Include Library → Add .ZIP Library… Dependencies: install **WebSockets** (by
Markus Sattler), **MQTTPubSubClient**, and **ArduinoJson** 6.x from the Library Manager.

## How loop() behaves

**Before 0.4.0, `loop()` blocked the calling sketch** for seconds at a time whenever
WiFi/MQTT was down — a `while(...) delay(...)` for WiFi (up to 20 s), one for NTP on
ESP8266 (up to 15 s), and one for the WSS handshake (up to 15 s). A sketch that read its
button only via `digitalRead()` right after `btn.loop()` lost every press that happened
during one of those windows (for example, while a device with revoked credentials kept
retrying, blocked stretches of 6-18 s were observed).

**Since 0.4.0**, `loop()` drives a small connection state machine and advances it by
*at most one step* per call — WiFi connect, ESP8266 NTP sync, and the WSS handshake are
each polled with `millis()` across calls instead of blocked on inside a single call.
Failures back off exponentially (3 s → 6 s → 12 s → 24 s → 48 s → 60 s, capped, reset to
3 s on the next successful MQTT connect) instead of retrying every 3 s forever — a
rejected device (bad token, deleted device) no longer hammers the broker.

> **Note (all boards): `loop()` freezes during each TLS handshake.** While the SDK is
> connecting or reconnecting, every connection attempt runs one TCP+TLS handshake inside
> the `WebSockets` library, and that handshake is **synchronous**: `loop()` does not return
> until it succeeds or fails. The library has no non-blocking connect API to poll instead.
> Measured on real boards:
>
> | Board | Successful handshake | Failed handshake |
> |---|---|---|
> | ESP8266 (BearSSL) | ~5 s | ~5 s |
> | ESP32 / ESP32-C6 (mbedTLS) | 1-5 s | up to ~15 s (one attempt observed at 15.0 s) |
>
> On ESP32 the TLS layer's own handshake timeout is much longer than that (arduino-esp32's
> default is 120 s) and the `WebSockets` library does not expose it, so a stalled server can
> in principle hold `loop()` even longer. While connected there is no handshake and
> `loop()` returns immediately.
>
> Plan your sketch for it: anything time-critical (button input, LEDs, relays, watchdogs)
> must not depend on `loop()` returning quickly while the device is offline. Use interrupts
> for input, as `examples/ButtonPress` does. Keep the watchdog timeout above the worst
> handshake above.

Everything else — the debounce logic, the HTTP Upgrade handshake once TCP+TLS is up, MQTT
CONNACK, and all of `CONNECTED`'s steady-state traffic — is either instant or bounded well
under that. `begin()` itself never blocks.

**Presses made while offline are not queued.** `emitButtonPress()` returns `false` when
there is no MQTT connection, and the press is dropped; nothing is replayed after reconnect.
Show the user something (an LED blink) if the press matters.

**This is why `examples/ButtonPress` latches the button in an interrupt** instead of
`digitalRead()`-ing it once per `loop()` iteration: an ISR timestamps every edge into a
ring buffer the instant it happens, so a press landing during a TLS handshake (see the
note above) is still recorded and correctly classified once `loop()` gets back around to
draining the buffer. See `ButtonGesture.h` for the debounce/chording logic, shared
between the example and `test/host/test_gesture.cpp`.

`state()` exposes the current connection-state-machine state (`ButtonConnect::State`) and
`nextRetryInMs()` returns the remaining backoff delay while in `BACKOFF` (0 otherwise) —
useful for a status LED or diagnostics; neither is required for normal use.

| From state | To state | When |
|---|---|---|
| WIFI_START | WIFI_WAIT | `WiFi.begin()` issued |
| WIFI_WAIT | CLOCK_WAIT (8266) / WS_START (32) | `WiFi.status() == WL_CONNECTED` |
| WIFI_WAIT | BACKOFF → WIFI_START | 20 s without connecting |
| CLOCK_WAIT | WS_START | NTP has set the clock (8266 only; skipped on ESP32) |
| CLOCK_WAIT | BACKOFF → CLOCK_WAIT | 15 s without NTP |
| WS_START | WS_WAIT | transport (re)initialized |
| WS_WAIT | MQTT_CONNECT | `_ws.isConnected()` |
| WS_WAIT | BACKOFF → WS_START | 15 s without a WS handshake (transport torn down) |
| MQTT_CONNECT | CONNECTED | broker CONNACK accepted (backoff resets) |
| MQTT_CONNECT | BACKOFF → WS_START | CONNECT failed: `[MQTT] CONNECT rejected (<reason>, err=N)` |
| CONNECTED | WIFI_START | WiFi link lost |
| CONNECTED | BACKOFF → WS_START | WS dropped (transport torn down) |
| CONNECTED | BACKOFF → MQTT_CONNECT | MQTT-only drop, WS still up |
| BACKOFF | (stored target) | delay elapsed, or immediately → WIFI_START if WiFi is down |

## Quick start


Get your `deviceId` + access token from the dashboard: **Add device → Bring your own
device → Button**. Then:

```cpp
#include <ButtonConnect.h>

ButtonConnect btn;

void setup() {
  ButtonConnectConfig cfg;
  cfg.wifiSsid     = "YOUR_WIFI";
  cfg.wifiPassword = "YOUR_WIFI_PASSWORD";
  cfg.deviceId     = "xxxxxxxxxxxxxxxx";
  cfg.deviceToken  = "xxxxxxxxxxxxxxxx";
  btn.begin(cfg);

  btn.onAck([](const String& result) {      // "on" | "off" | "ok" | "err"
    Serial.printf("ack: %s\n", result.c_str());
  });
}

void loop() {
  btn.loop();
  // …detect a press, then:
  btn.emitButtonPress(/*clicks=*/2, /*longs=*/0);   // double-click
}
```

See `examples/ButtonPress` for a full button with chord detection.

## API

| Method | Purpose |
|---|---|
| `begin(cfg)` | Configure WiFi + WSS transport + MQTT (non-blocking). |
| `loop()` | Pump the connection state machine one step; service incoming commands/acks. Call every loop. |
| `connected()` | WiFi + WSS + MQTT all up. |
| `state()` | Current connection state (`ButtonConnect::State`) — diagnostics only. |
| `nextRetryInMs()` | Remaining backoff delay while reconnecting, else 0 — diagnostics only. |
| `emitButtonPress(clicks, longs)` | Send a gesture chord → server triggers. |
| `emitEvent(type, gesture)` | Send a raw event. |
| `publishTelemetry(json)` | Publish a sensor reading. |
| `publishBattery(volts, pct)` | Publish battery state. |
| `onCommand(handler)` | Actuators: receive `{method, args}` commands. |
| `onAck(handler)` | Receive the server's press outcome (`on`/`off`/`ok`/`err`). |

`ButtonConnectConfig`: `wifiSsid`, `wifiPassword`, `deviceId`, `deviceToken`, and
optional `mqttHost` / `mqttPort` / `mqttPath` (default: the Button cloud WSS endpoint
`mq-server-01.buttonhq.io:443/`) and `rootCaPem`, and `ntpServer` (ESP8266 only; see below).

**Certificates: nothing to configure.** The Button cloud sits behind Cloudflare,
served with a public certificate, and the SDK bundles the two public roots behind it —
*ISRG Root X1* (Let's Encrypt) and *GTS Root R4* (Google Trust Services). Both are shipped
because Cloudflare re-issues that certificate unannounced and has moved between the two
issuers; trusting only one would strand your devices the day it rotates. Set `rootCaPem`
only when pointing at your own broker with its own CA.

## ESP8266: the TLS heap flag

**An ESP8266 build needs an extra flag or it will run out of memory during the TLS
handshake.** Cloudflare does not negotiate MFLN, so BearSSL allocates a full 16 KB receive
buffer — which does not fit alongside everything else in the ~40 KB heap. Enable the IRAM
second heap:

```ini
[env:esp8266]
platform  = espressif8266
board     = nodemcuv2
framework = arduino
build_flags =
  -DPIO_FRAMEWORK_ARDUINO_MMU_CACHE16_IRAM48_SECHEAP_SHARED
```

Symptoms without it: the WebSocket connects and the handshake then fails, or the device
reboots on connect. ESP32 has ample heap and needs nothing.

Trade-offs of the flag: slightly slower (16 KB cache plus IRAM byte-access emulation), and
IRAM buffers must not be touched from an ISR or by DMA — worth knowing if you also drive
interrupt-heavy peripherals.

## ESP8266: the clock must be set (handled by the SDK)

The ESP8266 has no battery-backed clock: after boot it reads 1970 until something sets it,
and BearSSL checks the server certificate's validity dates against that clock. With the
clock unset **every** TLS handshake fails ("Certificate is expired or not yet valid"), and
from the outside it just looks like a WebSocket that never connects. Whether the clock got
set used to depend on the WiFi router (some hand out an NTP server via DHCP, most home
routers don't), so the same firmware could work on one network and never on another.

Since 0.3.1 the SDK sets the clock itself before the first handshake, using NTP
(`pool.ntp.org`, with `time.google.com` and `time.cloudflare.com` as fallbacks), and logs
`[Time] clock set`. Nothing to configure. If your network blocks outbound NTP (UDP/123),
you will see `[Time] NTP sync failed` instead of a silent timeout — point
`cfg.ntpServer` at a reachable NTP server (e.g. your router). ESP32 is not affected.

## Topics

The SDK uses the `byod/{deviceId}/*` topics: `status`, `event`, `telemetry`,
`battery`, `cmd/ack` (publish) and `command` (subscribe). A device publishes and
subscribes only under its own `byod/{deviceId}/` prefix.

## Running the host tests

The gesture classifier (`src/ButtonGesture.h`) and the backoff calculator
(`src/internal/backoff.h`) are plain C++17 headers with no Arduino/hardware dependency,
specifically so their logic can be exercised on a dev machine instead of only on real
hardware. From the repo root:

```sh
g++ -std=c++17 -Wall -Wextra -o /tmp/test_backoff test/host/test_backoff.cpp && /tmp/test_backoff
g++ -std=c++17 -Wall -Wextra -o /tmp/test_gesture test/host/test_gesture.cpp && /tmp/test_gesture
```

Both print `... all tests passed` on success (plain `assert()`, no framework).

## License

Apache-2.0. See [LICENSE](LICENSE).
