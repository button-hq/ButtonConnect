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
#include "internal/backoff.h"

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
    // Advances one connection-state-machine step per call (see State below) instead of
    // blocking — see README "How loop() behaves" for the one call that can still block
    // briefly (the TCP+TLS handshake inside the WS library, ~1-3 s, up to ~5 s).
    void loop();
    bool connected();

    // Connection state machine driven one step per loop() call. Exposed for
    // diagnostics/tests; sketches normally only need connected().
    enum class State {
        WIFI_START,     // about to call WiFi.begin()
        WIFI_WAIT,      // WiFi.begin() issued, polling WiFi.status()
        CLOCK_WAIT,     // ESP8266 only: waiting for NTP to set the clock
        WS_START,       // about to (re-)initialize the WS/TLS transport
        WS_WAIT,        // pumping _ws.loop() until the WSS handshake completes
        MQTT_CONNECT,   // WS is up; issuing the bounded MQTT CONNECT
        CONNECTED,      // WiFi + WS + MQTT all up; servicing traffic
        BACKOFF,        // waiting out a failure's backoff delay before retrying
    };
    State state() const { return _state; }
    // Milliseconds until the next retry is attempted while in BACKOFF, else 0.
    unsigned long nextRetryInMs() const;

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
    // Each step* function advances exactly ONE state and returns to loop() — none of
    // them may block or spin; any wait is expressed as "check elapsed millis(), bail
    // out this call, get called again next loop()". See README "How loop() behaves".
    void stepWifiStart();
    void stepWifiWait(unsigned long now);
    void stepClockWait(unsigned long now);
    void stepWsStart();
    void stepWsWait(unsigned long now);
    void stepMqttConnect();
    void stepConnected();
    void stepBackoff(unsigned long now);
    void enterBackoff(State target, const char* logTag);

    void ensureTransport();
    bool ensureClock();   // one non-blocking check/kick of the ESP8266 NTP sync
    void teardownTransport();
    static bool needsClockWait();   // true on ESP8266 only

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
    unsigned long _seq = 0;

    State _state = State::WIFI_START;
    buttonconnect::Backoff _backoff;
    State _backoffTarget = State::WIFI_START;
    unsigned long _backoffUntilMs   = 0;
    unsigned long _wifiStartMs      = 0;
    unsigned long _clockWaitStartMs = 0;
    unsigned long _wsStartMs        = 0;
};
