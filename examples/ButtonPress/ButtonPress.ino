// ButtonConnect — Button source example.
// A GPIO button sends press GESTURES to the Button cloud on the byod/ plane; the server
// runs whatever triggers you configured in the dashboard and acks the outcome back.
//
// 1. Dashboard: Add device → Bring your own device → Button. Copy the deviceId + token.
// 2. Fill the credentials below (+ your WiFi).
// 3. Flash, open Serial @115200, and press: single / double / triple / long / click+long.

#include <ButtonConnect.h>

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

ButtonConnect btn;

static bool          lastReading = HIGH;
static bool          stableState = HIGH;
static unsigned long debounceAt  = 0;
static unsigned long pressStart  = 0;
static int           gClicks = 0, gLongs = 0;
static bool          chordOpen = false;
static unsigned long lastEdge  = 0;

void setup() {
  Serial.begin(115200);
  delay(200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

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
  btn.loop();                 // keeps WiFi + MQTT alive, services commands/acks
  unsigned long now = millis();

  // Debounced edges (active-low).
  bool reading = digitalRead(BUTTON_PIN);
  if (reading != lastReading) { debounceAt = now; lastReading = reading; }

  if ((now - debounceAt) >= DEBOUNCE_MS && reading != stableState) {
    stableState = reading;
    if (reading == LOW) {
      pressStart = now;                                  // press
    } else {
      unsigned long held = now - pressStart;             // release — classify
      if (held >= LONG_PRESS_MS) gLongs++; else gClicks++;
      chordOpen = true; lastEdge = now;
    }
  }

  // Chord complete → emit {clicks, longs}.
  if (chordOpen && stableState == HIGH && (now - lastEdge) >= GESTURE_GAP_MS) {
    int c = gClicks, l = gLongs;
    gClicks = 0; gLongs = 0; chordOpen = false;
    Serial.printf("[Button] gesture clicks=%d longs=%d\n", c, l);
    btn.emitButtonPress(c, l);
  }
}
