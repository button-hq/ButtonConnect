# ButtonConnect

The **BYOD device SDK** for the [Button](https://buttonhq.io) cloud. Connect your own
ESP32 / ESP8266 hardware to Button over TLS MQTT on the constrained `byod/{deviceId}/*`
plane — publish button gestures and telemetry, receive commands, and get press
acknowledgements back.

- **WiFi + TLS MQTT** with the Button root CA **bundled** — no certificate wrangling.
- **Gestures**: single / double / triple click and click+long chords map straight
  to the triggers you configure in the dashboard.
- **Commands**: built-in `reboot`; actuators handle their own methods via `onCommand`.

> This is the open-source connection layer extracted from Button's first-party firmware.

## Install

**PlatformIO** (`platformio.ini`):
```ini
lib_deps =
    ButtonConnect
    knolleary/PubSubClient @ ^2.8
    bblanchon/ArduinoJson @ ^6.21.0
platform = espressif32   ; or espressif8266
board = esp32dev         ; XIAO C6 button → seeed_xiao_esp32c6
framework = arduino
```

**Arduino IDE**: Sketch → Include Library → Add .ZIP Library… (or drop the folder in
`~/Documents/Arduino/libraries/`). Dependencies: install **PubSubClient** and
**ArduinoJson** from the Library Manager.

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
| `begin(cfg)` | Configure WiFi + TLS + MQTT (non-blocking). |
| `loop()` | Pump the connection; service incoming commands/acks. Call every loop. |
| `connected()` | WiFi + MQTT both up. |
| `emitButtonPress(clicks, longs)` | Send a gesture chord → server triggers. |
| `emitEvent(type, gesture)` | Send a raw event. |
| `publishTelemetry(json)` | Publish a sensor reading. |
| `publishBattery(volts, pct)` | Publish battery state. |
| `onCommand(handler)` | Actuators: receive `{method, args}` commands. |
| `onAck(handler)` | Receive the server's press outcome (`on`/`off`/`ok`/`err`). |

`ButtonConnectConfig`: `wifiSsid`, `wifiPassword`, `deviceId`, `deviceToken`, and
optional `mqttHost` / `mqttPort` (default: the Button cloud broker).

## Topics

The SDK uses the constrained `byod/{deviceId}/*` plane: `status`, `event`, `telemetry`,
`battery`, `cmd/ack` (publish) and `command` (subscribe). The device is confined to its
own topics by the server ACL.

## License

Apache-2.0. See [LICENSE](LICENSE).
