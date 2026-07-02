#pragma once
#include <Arduino.h>
#include <functional>

#if defined(ESP32)
  #include <WiFi.h>
  #include <WiFiClientSecure.h>
#else
  #include <ESP8266WiFi.h>
#endif
#include <PubSubClient.h>

// ── Button Connect SDK ───────────────────────────────────────────────────────
// Connects an ESP device to the Button cloud (buttonhq.io) over TLS MQTT on the
// constrained `byod/{deviceId}/*` plane. This is the connection/transport layer
// extracted from the first-party firmware as the open-source baseline — no FOTA,
// no deep sleep, no provisioning: credentials are supplied by the caller.
//
// A *source* (button/sensor) publishes events/telemetry. An *actuator* (relay/IR)
// additionally sets an onCommand() handler to receive commands. The server ACL
// decides what a given device may actually do — see the integration strategy §5.3.

struct ButtonConnectConfig {
    const char* wifiSsid;
    const char* wifiPassword;
    const char* mqttHost   = "mq-server-01.iothub.ge";
    uint16_t    mqttPort   = 8883;
    const char* deviceId   = "";
    const char* deviceToken = "";
};

class ButtonConnect {
public:
    // method = e.g. "relay.set" | "ir.transmit"; argsJson = the command's raw "args" object.
    using CommandHandler = std::function<void(const String& method, const String& argsJson)>;

    // Configure WiFi + TLS + MQTT. Does not block on the network; loop() drives connect.
    void begin(const ButtonConnectConfig& cfg);
    // Pump WiFi/MQTT: reconnect if dropped, service incoming messages. Call every loop().
    void loop();
    bool connected();

    // ── Source → server (publish) ──
    // Fire a discrete event that drives the owner's server-side triggers.
    // e.g. emitEvent("button.pressed", "click"). seq auto-increments for de-dup.
    bool emitEvent(const char* type, const char* gesture = nullptr);
    // Fire a button-press GESTURE as a chord: clicks = short taps, longs = long holds.
    // e.g. (2,0) double-click, (3,0) triple, (1,1) click+long, (0,1) long. Matches the
    // server-side trigger matrix (ButtonTrigger.Clicks / Longs).
    bool emitButtonPress(int clicks, int longs);
    // Raw telemetry object, e.g. publishTelemetry("{\"temp\":22.5}").
    bool publishTelemetry(const String& json);
    bool publishBattery(float volts, int percent);

    // ── Actuator: receive commands (byod/{id}/command) ──
    // Setting a handler makes the device subscribe to its command topic on connect.
    // The SDK auto-acks (byod/{id}/cmd/ack) after the handler returns.
    void onCommand(CommandHandler handler) { _onCommand = handler; }

    // ── Server → device: button-press acknowledgement ──
    // After a press, the server reports the outcome: "on"/"off" (a relay it toggled),
    // "ok" (a stateless action fired), or "err" (nothing bound / an action failed).
    using AckHandler = std::function<void(const String& result)>;
    void onAck(AckHandler handler) { _onAck = handler; }

private:
    void connectWifi();
    bool connectMqtt();
    void onMqttMessage(char* topic, uint8_t* payload, unsigned int len);
    void ack(const String& id, const char* status, const char* error = nullptr);

    ButtonConnectConfig _cfg;
#if defined(ESP32)
    WiFiClientSecure _tls;
#else
    BearSSL::WiFiClientSecure _tls;
#endif
    PubSubClient _mqtt{_tls};

    String _base;          // "byod/{deviceId}/"
    String _topicStatus, _topicEvent, _topicTelemetry, _topicBattery, _topicCommand, _topicCmdAck;
    CommandHandler _onCommand;
    AckHandler _onAck;
    unsigned long _lastReconnectAttempt = 0;
    unsigned long _seq = 0;

    static ButtonConnect* _self;   // trampoline target for the C-style PubSubClient callback
    static void mqttTrampoline(char* topic, uint8_t* payload, unsigned int len);
};
