// ButtonConnect — Button source example.
// A GPIO button sends press GESTURES to the Button cloud on the byod/ plane; the server
// runs whatever triggers you configured in the dashboard and acks the outcome back.
//
// 1. Dashboard: Add device → Bring your own device → Button. Copy the deviceId + token.
// 2. Fill the credentials below (+ your WiFi).
// 3. Flash, open Serial @115200, and press: single / double / triple / long / click+long.
//
// btn.loop() still has one unavoidable blocking call inside it — the initial TCP+TLS
// handshake performed by the WebSockets library (typically 1-3 s, up to ~5 s worst
// case). A digitalRead() done only once per loop() iteration would miss presses that
// land during that window. So the button is latched in an interrupt instead: an ISR records every edge's
// timestamp into a small ring buffer the instant it happens, and loop() drains that
// buffer through the SAME debounce/classify/chord state machine (ButtonGesture.h,
// shared with the SDK's own host-side unit tests) whether or not it was delayed getting
// around to it.

#include <ButtonConnect.h>
#include <ButtonGesture.h>

static const char*    WIFI_SSID     = "YOUR_WIFI";
static const char*    WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
static const char*    DEVICE_ID     = "byod-xxxxxxxxxxxxxxxx";
static const char*    DEVICE_TOKEN  = "REPLACE_WITH_64_HEX_ACCESS_TOKEN";

// Boot button on most ESP32 devkits (GPIO0, active-low). XIAO C6 button = GPIO2.
#ifndef BUTTON_PIN
  #define BUTTON_PIN 0
#endif

static const unsigned long DEBOUNCE_MS    = 40;
static const unsigned long LONG_PRESS_MS  = 500;   // hold ≥ this → a "long", else a "click"
static const unsigned long GESTURE_GAP_MS = 400;   // idle after release → emit the chord
static const size_t        RING_CAPACITY  = 32;    // edges buffered between loop() calls

ButtonConnect btn;

// Producer (ISR) / consumer (loop()) split: the ISR only ever pushes a (level, time)
// pair — no debounce, no String, no classification — so it stays IRAM-safe and fast.
// All logic lives in GestureClassifier (src/ButtonGesture.h), fed from the ring.
static buttonconnect::EdgeRing<RING_CAPACITY>       edgeRing;
static buttonconnect::GestureClassifier             gesture(DEBOUNCE_MS, LONG_PRESS_MS,
                                                              GESTURE_GAP_MS, /*activeLow=*/true);
static unsigned long lastOverflowLogged = 0;

// IRAM_ATTR is defined by both the ESP32 and ESP8266 Arduino cores (ESP8266's core also
// `#define`s it, historically named ICACHE_RAM_ATTR); using the shared macro name keeps
// this one ISR portable across both without an #ifdef.
void IRAM_ATTR buttonIsr() {
    edgeRing.push(digitalRead(BUTTON_PIN), millis());
}

void setup() {
  Serial.begin(115200);
  delay(200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonIsr, CHANGE);

  ButtonConnectConfig cfg;
  cfg.wifiSsid     = WIFI_SSID;
  cfg.wifiPassword = WIFI_PASSWORD;
  cfg.deviceId     = DEVICE_ID;
  cfg.deviceToken  = DEVICE_TOKEN;
  // cfg.mqttHost / cfg.mqttPort default to the Button cloud broker.
  btn.begin(cfg);

  // The server reports what our press did: "on"/"off" (a relay), "ok" (fired), "err" (none).
  btn.onAck([](const String& result) {
    Serial.printf("[Button] server ack → %s\n", result.c_str());
  });

  Serial.println("[Button] ready — click / double / triple / long / click+long.");
}

void loop() {
  btn.loop();                 // keeps WiFi + MQTT alive, services commands/acks (never waits; see README)
  unsigned long now = millis();

  // Drain every edge the ISR captured since the last loop() call, in order, through the
  // shared classifier — this is what makes a press during the one remaining blocking
  // handshake still count instead of being missed.
  buttonconnect::GestureEdge edge;
  while (edgeRing.pop(edge)) {
    gesture.onEdge(edge.level, edge.atMs);
  }
  // Correct the state if the last edge was dropped by the debounce (see sync()).
  gesture.sync(digitalRead(BUTTON_PIN), now);
  if (edgeRing.overflowCount() != lastOverflowLogged) {
    lastOverflowLogged = edgeRing.overflowCount();
    Serial.printf("[Button] edge ring overflow, dropped=%lu\n", lastOverflowLogged);
  }

  // Chord complete → emit {clicks, longs}.
  if (gesture.chordReady(now)) {
    buttonconnect::GestureResult r = gesture.takeChord();
    Serial.printf("[Button] gesture clicks=%d longs=%d\n", r.clicks, r.longs);
    if (!btn.emitButtonPress(r.clicks, r.longs)) {
      Serial.println("[Button] offline — gesture not sent");
    }
  }
}
