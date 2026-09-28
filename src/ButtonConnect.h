#pragma once
#include <Arduino.h>
#include <functional>

#if defined(ESP32)
  #include <WiFi.h>
#else
  #include <ESP8266WiFi.h>
#endif
#include <WebSocketsClient.h>   // MUST precede MQTTPubSubClient.h to enable the WS transport
#include <MQTTPubSubClient.h>

// ── Button Connect SDK ───────────────────────────────────────────────────────
// Connects an ESP device to the Button cloud (buttonhq.io) over MQTT-over-WSS on
// the constrained `byod/{deviceId}/*` plane. This is the connection/transport layer
// extracted from the first-party firmware as the open-source baseline — no FOTA,
// no deep sleep, no provisioning: credentials are supplied by the caller.
//
// Transport: secure WebSockets (wss://host:443/) → Cloudflare tunnel → mosquitto's
// plaintext websockets listener. The broker is never exposed directly. TLS server
// validation is MANDATORY (see rootCaPem below); auth is deviceId+token + the
// server-side ACL, which decides what a given device may actually do (strategy §5.3).
//
// A *source* (button/sensor) publishes events/telemetry. An *actuator* (relay/IR)
// additionally sets an onCommand() handler to receive commands.

// MQTT read/write buffer. MQTTPubSubClient<N> reserves two N-byte static buffers;
// must hold one inbound command with inline ir.transmit timings. Override with
// -DBUTTON_MQTT_BUF=… to trade RAM for larger payloads.
#ifndef BUTTON_MQTT_BUF
  #define BUTTON_MQTT_BUF 4096
#endif

struct ButtonConnectConfig {
    const char* wifiSsid;
    const char* wifiPassword;
    // Cloudflare-fronted WSS endpoint (host:443, path "/"). The device connects with
    // wss:// and the "mqtt" subprotocol.
    const char* mqttHost   = "mq-server-01.buttonhq.io";
    uint16_t    mqttPort   = 443;
    const char* mqttPath   = "/";
    const char* deviceId   = "";
    const char* deviceToken = "";
    // Trust anchor(s) as PEM. Leave null for the Button cloud: the SDK bundles the
    // public roots behind Cloudflare's edge certificate (ISRG Root X1 and GTS Root
    // R4), so the defaults connect as-is and survive Cloudflare rotating between the
    // two. Set this only to point at a broker with its own CA — a self-hosted or
    // local mosquitto. A concatenated multi-cert PEM is accepted.
    const char* rootCaPem  = nullptr;
    // NTP server used to set the clock before the TLS handshake (ESP8266 only — see
    // ensureClock()). Two public fallbacks are always tried as well. Point this at a
    // local NTP server on networks that block outbound UDP/123 to the internet.
    const char* ntpServer  = "pool.ntp.org";
};

class ButtonConnect {
public:
    // method = e.g. "relay.set" | "ir.transmit"; argsJson = the command's raw "args" object.
    using CommandHandler = std::function<void(const String& method, const String& argsJson)>;

    // Configure WiFi + WSS transport + MQTT. Does not block on the network; loop() drives connect.
    void begin(const ButtonConnectConfig& cfg);
    // Pump WiFi/WS/MQTT: reconnect if dropped, service incoming messages. Call every loop().
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
    void ensureTransport();
    bool ensureClock();
    void onCommandPayload(const String& payload);
    void ack(const String& id, const char* status, const char* error = nullptr);

    ButtonConnectConfig _cfg;
    WebSocketsClient _ws;
    MQTTPubSub::PubSubClient<BUTTON_MQTT_BUF> _mqtt;

    String _base;          // "byod/{deviceId}/"
    String _topicStatus, _topicEvent, _topicTelemetry, _topicBattery, _topicCommand, _topicCmdAck;
    CommandHandler _onCommand;
    AckHandler _onAck;
    bool _transportBegun = false;
    bool _ntpStarted = false;
    unsigned long _lastReconnectAttempt = 0;
    unsigned long _seq = 0;
};
