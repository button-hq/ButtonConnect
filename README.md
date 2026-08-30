# ButtonConnect

The **BYOD device SDK** for the [Button](https://buttonhq.io) cloud. Connect your own
ESP32 / ESP8266 hardware to Button over **MQTT-over-WSS** (secure WebSockets through the
Cloudflare edge) on the constrained `byod/{deviceId}/*` plane — publish button gestures
and telemetry, receive commands, and get press acknowledgements back.

- **WiFi + MQTT over secure WebSockets** with mandatory TLS server validation — the
  broker is never exposed directly.
- **Gestures**: single / double / triple click and click+long chords map straight
  to the triggers you configure in the dashboard.
- **Commands**: built-in `reboot`; actuators handle their own methods via `onCommand`.

> This is the open-source connection layer extracted from Button's first-party firmware.

## Install

> Not yet in the PlatformIO / Arduino registries — install straight from GitHub.

**PlatformIO** (`platformio.ini`):
```ini
lib_deps =
    https://github.com/button-hq/ButtonConnect.git
    links2004/WebSockets @ ^2.4.1
    hideakitai/MQTTPubSubClient @ ^0.2.0
    bblanchon/ArduinoJson @ ^6.21.0
platform = espressif32   ; or espressif8266
board = esp32dev         ; XIAO C6 button → seeed_xiao_esp32c6
framework = arduino
```

**Arduino IDE**: download the repo as a ZIP
(`https://github.com/button-hq/ButtonConnect` → Code → Download ZIP), then Sketch →
Include Library → Add .ZIP Library… Dependencies: install **WebSockets** (by Markus
Sattler), **MQTTPubSubClient**, and **ArduinoJson** from the Library Manager.

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
| `loop()` | Pump the connection; service incoming commands/acks. Call every loop. |
| `connected()` | WiFi + WSS + MQTT all up. |
| `emitButtonPress(clicks, longs)` | Send a gesture chord → server triggers. |
| `emitEvent(type, gesture)` | Send a raw event. |
| `publishTelemetry(json)` | Publish a sensor reading. |
| `publishBattery(volts, pct)` | Publish battery state. |
| `onCommand(handler)` | Actuators: receive `{method, args}` commands. |
| `onAck(handler)` | Receive the server's press outcome (`on`/`off`/`ok`/`err`). |

`ButtonConnectConfig`: `wifiSsid`, `wifiPassword`, `deviceId`, `deviceToken`, and
optional `mqttHost` / `mqttPort` / `mqttPath` (default: the Button cloud WSS endpoint
`mq-server-01.buttonhq.io:443/`) and `rootCaPem`.

**Certificates: nothing to configure.** The Button cloud sits behind a Cloudflare tunnel
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

## Topics

The SDK uses the constrained `byod/{deviceId}/*` plane: `status`, `event`, `telemetry`,
`battery`, `cmd/ack` (publish) and `command` (subscribe). The device is confined to its
own topics by the server ACL.

## License

Apache-2.0. See [LICENSE](LICENSE).
