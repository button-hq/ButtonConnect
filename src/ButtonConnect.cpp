#include "ButtonConnect.h"
#include <ArduinoJson.h>
#include <time.h>

// WebSocket handshake timeout (the WS transport must be up before MQTT CONNECT can ride it).
#define BUTTON_WS_CONNECT_TIMEOUT_MS 15000

// ESP8266: how long one connect attempt waits for NTP before giving up (and retrying on the
// next attempt). Earliest wall-clock time accepted as "set": anything before it means the
// clock is still at its power-on value (the Unix epoch).
#ifndef BUTTON_NTP_TIMEOUT_MS
  #define BUTTON_NTP_TIMEOUT_MS 15000
#endif
#define BUTTON_MIN_VALID_EPOCH 1704067200UL   // 2024-01-01T00:00:00Z

// Default trust anchors, used when ButtonConnectConfig.rootCaPem is null.
//
// The Button cloud is reached over wss:// through a Cloudflare tunnel, and Cloudflare
// serves that endpoint with a PUBLIC certificate — so the anchor has to be the public
// root behind it, not our own private CA.
//
// BOTH public roots are bundled deliberately. Cloudflare re-issues the edge certificate
// unannounced and has moved between Let's Encrypt and Google Trust Services; pinning a
// single root would strand every device in the field the day it rotates, and BYOD
// hardware is precisely what cannot be reflashed remotely. Carrying both costs a little
// flash and removes that failure mode entirely.
//
// A concatenated multi-cert PEM works on both platforms: ESP32/mbedTLS parses the whole
// buffer, and ESP8266's X509List::append() reads every certificate in it and builds a
// trust anchor for each.
//
// Connecting to a self-hosted broker with its own CA? Pass cfg.rootCaPem instead.
static const char BUTTON_ROOT_CA[] PROGMEM = R"CERT(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIICCTCCAY6gAwIBAgINAgPlwGjvYxqccpBQUjAKBggqhkjOPQQDAzBHMQswCQYD
VQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIG
A1UEAxMLR1RTIFJvb3QgUjQwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAw
WjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz
IExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjQwdjAQBgcqhkjOPQIBBgUrgQQAIgNi
AATzdHOnaItgrkO4NcWBMHtLSZ37wWHO5t5GvWvVYRg1rkDdc/eJkTBa6zzuhXyi
QHY7qca4R9gq55KRanPpsXI5nymfopjTX15YhmUPoYRlBtHci8nHc8iMai/lxKvR
HYqjQjBAMA4GA1UdDwEB/wQEAwIBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW
BBSATNbrdP9JNqPV2Py1PsVq8JQdjDAKBggqhkjOPQQDAwNpADBmAjEA6ED/g94D
9J+uHXqnLrmvT/aDHQ4thQEd0dlq7A/Cr8deVl5c1RxYIigL9zC2L7F8AjEA8GE8
p/SgguMh1YQdc4acLa/KNJvxn7kjNuK8YAOdgLOaVsjh4rsUecrNIdSUtUlD
-----END CERTIFICATE-----
)CERT";

void ButtonConnect::begin(const ButtonConnectConfig& cfg) {
    _cfg = cfg;

    _base           = String("byod/") + _cfg.deviceId + "/";
    _topicStatus    = _base + "status";
    _topicEvent     = _base + "event";
    _topicTelemetry = _base + "telemetry";
    _topicBattery   = _base + "battery";
    _topicCommand   = _base + "command";
    _topicCmdAck    = _base + "cmd/ack";

    WiFi.mode(WIFI_STA);
    // The WSS transport (WebSocketsClient + MQTTPubSubClient) is initialized lazily in
    // ensureTransport() on the first connect so a failed handshake can tear it down and
    // free the TLS buffer before the next retry.
    _state = State::WIFI_START;
    _backoff.reset();
}

// Human-readable reason for a failed MQTT CONNECT, from lwmqtt_err_t (lwmqtt.h).
static const char* connectErrorText(int err) {
    switch (err) {
        case -10: return "refused by broker: bad credentials or unknown device";  // LWMQTT_CONNECTION_DENIED
        case -4:  return "no CONNACK before timeout";                              // LWMQTT_NETWORK_TIMEOUT
        case -9:  return "unexpected packet instead of CONNACK";                   // LWMQTT_MISSING_OR_WRONG_PACKET
        case -5:  return "read failed (connection closed)";                        // LWMQTT_NETWORK_FAILED_READ
        case -6:  return "write failed (connection closed)";                       // LWMQTT_NETWORK_FAILED_WRITE
        case -3:  return "transport not connected";                                // LWMQTT_NETWORK_FAILED_CONNECT
        default:  return "error";
    }
}

// ── Non-blocking connection state machine ──────────────────────────────────
//
// loop() used to block the caller for seconds at a time (WiFi.begin + 20 s poll loop,
// a 15 s NTP wait, a 15 s WS-handshake wait, all with delay() inside) — a sketch that
// reads its button after btn.loop() lost every press that happened during an outage.
//
// Every one of those waits is now a *state*, and loop() advances the state machine by
// exactly one step (never more) per call, using millis() deltas *across calls* instead
// of a local while/delay. The one wait that cannot be removed without replacing the
// transport library is the initial TCP+TLS handshake inside WebSocketsClient::loop()
// (~5 s on ESP8266; 1-5 s on ESP32 and up to ~15 s when it fails, measured — the
// handshake itself, not our polling of it, blocks); see WS_WAIT below and the README.
//
//   WIFI_START   → WiFi.begin() once, immediately → WIFI_WAIT
//   WIFI_WAIT    → poll WiFi.status(); on connect → CLOCK_WAIT (8266) / WS_START (32);
//                  after 20 s → BACKOFF → WIFI_START
//   CLOCK_WAIT   → (ESP8266 only) poll time(nullptr) after one configTime(); on set →
//                  WS_START; after 15 s → BACKOFF → CLOCK_WAIT
//   WS_START     → ensureTransport() (begin, idempotent) → WS_WAIT
//   WS_WAIT      → pump _ws.loop() once; on isConnected() → MQTT_CONNECT; after 15 s →
//                  tear down (frees the BearSSL buffer) → BACKOFF → WS_START
//   MQTT_CONNECT → (WS already up) bounded MQTT CONNECT; on success → CONNECTED
//                  (backoff resets to 3 s); on reject → BACKOFF → MQTT_CONNECT
//   CONNECTED    → pump _ws.loop() + _mqtt.update(); on WS drop → tear down → BACKOFF →
//                  WS_START; on MQTT-only drop → BACKOFF → MQTT_CONNECT
//   BACKOFF      → wait out 3 s..60 s (doubling, logged once) → jump to the stored
//                  target state
//
// A WiFi drop detected in ANY state above WIFI_WAIT (except while already backing off
// to retry WiFi itself) forces an immediate restart at WIFI_START — there is no point
// waiting out an MQTT retry timer against a dead link.
void ButtonConnect::stepWifiStart() {
    if (WiFi.status() == WL_CONNECTED) {
        _clockWaitStartMs = millis();
        _state = needsClockWait() ? State::CLOCK_WAIT : State::WS_START;
        return;
    }
    Serial.printf("[WiFi] connecting to %s ...\n", _cfg.wifiSsid);
    // Cancel the previous attempt before starting a new one. On ESP32 the driver keeps
    // trying to join after our 20 s timeout, and while it is still "connecting" a new
    // WiFi.begin() is refused ("sta is connecting, return error" / ESP_ERR_WIFI_CONN), so
    // without this every retry was a no-op and the device never recovered. Not done on the
    // very first attempt (nothing to cancel). ESP32 waits at most ~100 ms here.
    if (_wifiAttempted) WiFi.disconnect();
    _wifiAttempted = true;
    WiFi.begin(_cfg.wifiSsid, _cfg.wifiPassword);   // called ONCE per attempt, not per loop()
    _wifiStartMs = millis();
    _state = State::WIFI_WAIT;
}

void ButtonConnect::stepWifiWait(unsigned long now) {
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WiFi] connected, ip=%s\n", WiFi.localIP().toString().c_str());
        _clockWaitStartMs = now;
        _state = needsClockWait() ? State::CLOCK_WAIT : State::WS_START;
        return;
    }
    if (now - _wifiStartMs >= 20000) {
        Serial.println("[WiFi] connect timed out — will retry");
        enterBackoff(State::WIFI_START, "[WiFi]");
    }
    // else: nothing to do this call — WiFi.begin() is already in flight.
}

// Initialize the secure-WebSocket transport + MQTT-over-WS layer (idempotent per session).
void ButtonConnect::ensureTransport() {
    if (_transportBegun) return;

    // Governs how often WebSocketsClient::loop() itself retries the blocking TCP+TLS
    // connect while we're in WS_WAIT (see stepWsWait below) — NOT how often *we* retry
    // overall; the SDK's own backoff (3 s..60 s, doubling) covers that and is enforced
    // by simply not calling _ws.loop() at all while in BACKOFF (see loop()'s switch).
    // Left at the library's already-reasonable 3 s so a WS_WAIT window (15 s) gets a
    // handful of TCP attempts rather than just one.
    _ws.setReconnectInterval(3000);
    // WS-level ping keeps the connection warm under Cloudflare's ~100s idle cutoff and
    // detects half-open links (30s ping, 6s pong timeout, drop after 2 misses).
    _ws.enableHeartbeat(30000, 6000, 2);

    // Plain WSS with MANDATORY server validation (no client cert / mTLS — not meaningful
    // on a chip that can't protect a private key). Auth is deviceId+token + server ACL.
    const char* ca = _cfg.rootCaPem ? _cfg.rootCaPem : BUTTON_ROOT_CA;
#if defined(ESP32)
    _ws.beginSslWithCA(_cfg.mqttHost, _cfg.mqttPort, _cfg.mqttPath, ca, "mqtt");
#else
    // ESP8266 / BearSSL: the parsed trust anchor must outlive the connection.
    static BearSSL::X509List* caList = nullptr;
    static const char*        caInUse = nullptr;
    if (!caList || caInUse != ca) {
        delete caList;
        caInUse = ca;
        caList  = new BearSSL::X509List(ca);
    }
    _ws.beginSslWithCA(_cfg.mqttHost, _cfg.mqttPort, _cfg.mqttPath, caList, "mqtt");
#endif

    _mqtt.begin(_ws);
    _mqtt.setKeepAliveTimeout(60);   // MQTT keepalive < Cloudflare's ~100s WS idle cutoff
    _transportBegun = true;
}

void ButtonConnect::teardownTransport() {
    // Tear the WS/TLS down so its BearSSL buffer is freed — otherwise each failed
    // attempt leaks it and the next retry can OOM. ensureTransport() re-inits it fresh
    // on the next WS_START; no new heap churn happens anywhere else (CONNECTED never
    // calls this).
    _ws.disconnect();
    _transportBegun = false;
}

bool ButtonConnect::needsClockWait() {
#if defined(ESP8266)
    return true;
#else
    return false;   // arduino-esp32's mbedTLS does not fail on an unset clock.
#endif
}

// ESP8266 / BearSSL checks the server certificate's validity dates against the system
// clock, and the ESP8266 has no RTC: after boot the clock reads the Unix epoch (1970) until
// something sets it. With the clock at 1970 every handshake fails with "Certificate is
// expired or not yet valid" — silently, as a WebSocket that never comes up. Whether the
// clock happened to get set depended on the network (some routers hand out an NTP server
// via DHCP, most home routers do not), so the same firmware worked on one WiFi and never
// connected on another. Set it explicitly before the first handshake.
// (ESP32/mbedTLS in arduino-esp32 does not fail on an unset clock, so this is 8266-only.)
//
// Non-blocking: one check + at most one configTime() kick per call. The 15 s deadline is
// enforced by the caller (stepClockWait), measured across calls via millis(), not here.
bool ButtonConnect::ensureClock() {
#if defined(ESP8266)
    if (time(nullptr) >= (time_t)BUTTON_MIN_VALID_EPOCH) {
        if (_ntpStarted) {   // we were waiting on it — log the transition exactly once
            Serial.printf("[Time] clock set, epoch=%lu\n", (unsigned long)time(nullptr));
            _ntpStarted = false;
        }
        return true;
    }
    if (!_ntpStarted) {
        const char* primary = (_cfg.ntpServer && *_cfg.ntpServer) ? _cfg.ntpServer : "pool.ntp.org";
        configTime(0, 0, primary, "time.google.com", "time.cloudflare.com");
        _ntpStarted = true;
        Serial.printf("[Time] clock not set; syncing via NTP (%s) ...\n", primary);
    }
    return false;
#else
    return true;
#endif
}

void ButtonConnect::stepClockWait(unsigned long now) {
#if defined(ESP8266)
    if (!_ntpStarted) _clockWaitStartMs = now;   // (re)starting an attempt — reset the deadline
    if (ensureClock()) {
        _state = State::WS_START;
        return;
    }
    if (now - _clockWaitStartMs >= BUTTON_NTP_TIMEOUT_MS) {
        Serial.println("[Time] NTP sync failed — TLS certificate check cannot pass; will retry "
                       "(is outbound UDP/123 blocked? set cfg.ntpServer to a reachable server)");
        _ntpStarted = false;   // force a fresh configTime() on the next attempt
        enterBackoff(State::CLOCK_WAIT, "[Time]");
    }
    // else: NTP reply hasn't arrived yet — nothing to do this call.
#else
    _state = State::WS_START;   // unreachable (needsClockWait() is false), kept defensive
#endif
}

void ButtonConnect::stepWsStart() {
    ensureTransport();
    Serial.printf("[MQTT] WSS connect to wss://%s:%u%s ...\n",
                  _cfg.mqttHost, _cfg.mqttPort, _cfg.mqttPath);
    _wsStartMs = millis();
    _state = State::WS_WAIT;
}

void ButtonConnect::stepWsWait(unsigned long now) {
    // The WebSocket handshake is async at the protocol level, but WebSocketsClient::loop()
    // performs the initial TCP+TLS connect() SYNCHRONOUSLY the first time (and again every
    // setReconnectInterval() while still failed) — this is the one blocking call left in
    // the SDK (~5 s on ESP8266; up to ~15 s on ESP32 when it fails, measured; see
    // links2004/WebSockets WebSocketsClient::loop()/connect paths). Everything after that
    // (the HTTP Upgrade handshake) is pumped incrementally by later loop() calls
    // (handleClientData()), which is why calling this once per SDK loop() call is correct
    // and sufficient — no local while/delay needed.
    _ws.loop();
    if (_ws.isConnected()) {
        _state = State::MQTT_CONNECT;
        return;
    }
    if (now - _wsStartMs >= BUTTON_WS_CONNECT_TIMEOUT_MS) {
        Serial.println("[MQTT] WSS transport not up (handshake/timeout)");
        teardownTransport();
        enterBackoff(State::WS_START, "[MQTT]");
    }
}

void ButtonConnect::stepMqttConnect() {
    // Reached only once WS_WAIT has already observed _ws.isConnected() == true (or
    // CONNECTED demoted us here after an MQTT-only drop with the WS still up). Guard
    // against a drop in between anyway.
    if (!_ws.isConnected()) {
        // The WS dropped while we were backing off (or between states). No attempt failed
        // here, so don't consume another backoff step — rebuild the transport now.
        teardownTransport();
        _state = State::WS_START;
        return;
    }

    // MQTTPubSubClient::connect() has its own "wait for WS" busy-loop
    // (`while(!client->isConnected()) { client->loop(); delay(10); }`), but since we only
    // ever call connect() with the WS already up, that loop's condition is false on its
    // very first check and it never actually spins. The real remaining wait is the
    // CONNACK round-trip inside lwmqtt_connect(), which we bound with setTimeout() —
    // normally tens of ms over an already-open WS, so 2 s is a generous ceiling, not a
    // typical wait.
    _mqtt.setTimeout(2000);
    _mqtt.setWill(_topicStatus.c_str(), "{\"online\":false}", false, 1);
    if (!_mqtt.connect(_cfg.deviceId, _cfg.deviceId, _cfg.deviceToken)) {
        // MQTTPubSubClient never copies the CONNACK return code into getReturnCode() (it
        // stays 0 = "accepted"), so report the lwmqtt error instead: it distinguishes a
        // broker refusal (CONNACK with a non-zero code) from no answer at all.
        const int err = (int)_mqtt.getLastError();
        Serial.printf("[MQTT] CONNECT rejected (%s, err=%d)\n", connectErrorText(err), err);
        // A failed connect() makes MQTTPubSubClient call close(), which disconnects the
        // WebSocket too — so the retry has to start from a fresh transport, not just a
        // new CONNECT. Tearing down here also frees the BearSSL buffer while we back off.
        teardownTransport();
        enterBackoff(State::WS_START, "[MQTT]");
        return;
    }
    Serial.println("[MQTT] connected");
    _backoff.reset();   // a real outage's retry ladder shouldn't linger after recovery

    // Always subscribe to the command topic — even a source device receives management
    // commands (e.g. reboot). Actuation methods are dispatched to onCommand (if set).
    // Per-topic callback delivers the raw payload; we only subscribe to `command`, so
    // no topic disambiguation is needed.
    _mqtt.subscribe(_topicCommand, [this](const char* payload, const size_t size) {
        onCommandPayload(String((const char*)payload).substring(0, size));
    });

    String status = String("{\"online\":true,\"ip\":\"") + WiFi.localIP().toString() +
                    "\",\"ssid\":\"" + WiFi.SSID() + "\",\"rssi\":" + String(WiFi.RSSI()) + "}";
    _mqtt.publish(_topicStatus.c_str(), status.c_str());
    _state = State::CONNECTED;
}

void ButtonConnect::stepConnected() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] connection lost — reconnecting");
        teardownTransport();
        _state = State::WIFI_START;
        return;
    }
    _ws.loop();       // pump the WebSocket (rx + heartbeat)
    if (!_ws.isConnected()) {
        Serial.println("[MQTT] WSS connection lost — reconnecting");
        teardownTransport();
        enterBackoff(State::WS_START, "[MQTT]");
        return;
    }
    _mqtt.update();   // process MQTT + fire subscription callbacks
    if (!_mqtt.isConnected()) {
        Serial.println("[MQTT] connection lost — reconnecting");
        // MQTT-level loss (keepalive/protocol error) with the WS still up: retry the
        // CONNECT; stepMqttConnect falls back to WS_START if the WS is gone by then.
        enterBackoff(State::MQTT_CONNECT, "[MQTT]");
    }
}

void ButtonConnect::enterBackoff(State target, const char* logTag) {
    unsigned long delayMs = _backoff.onFailure();
    Serial.printf("%s retry in %lu s\n", logTag, delayMs / 1000UL);
    _backoffTarget  = target;
    _backoffUntilMs = millis() + delayMs;
    _state = State::BACKOFF;
}

void ButtonConnect::stepBackoff(unsigned long now) {
    // Don't wait out a WS/MQTT retry timer against a link that's already dead — go
    // straight back to WIFI_START. (If we're already backing off to retry WiFi itself,
    // fall through to the normal timer below.)
    if (WiFi.status() != WL_CONNECTED && _backoffTarget != State::WIFI_START) {
        teardownTransport();
        _state = State::WIFI_START;
        return;
    }
    if ((long)(now - _backoffUntilMs) >= 0) {
        _state = _backoffTarget;
    }
}

unsigned long ButtonConnect::nextRetryInMs() const {
    if (_state != State::BACKOFF) return 0;
    unsigned long now = millis();
    long remaining = (long)(_backoffUntilMs - now);
    return remaining > 0 ? (unsigned long)remaining : 0;
}

void ButtonConnect::loop() {
    unsigned long now = millis();
    switch (_state) {
        case State::WIFI_START:   stepWifiStart();    break;
        case State::WIFI_WAIT:    stepWifiWait(now);  break;
        case State::CLOCK_WAIT:   stepClockWait(now); break;
        case State::WS_START:     stepWsStart();      break;
        case State::WS_WAIT:      stepWsWait(now);    break;
        case State::MQTT_CONNECT: stepMqttConnect();  break;
        case State::CONNECTED:    stepConnected();    break;
        case State::BACKOFF:      stepBackoff(now);   break;
    }
}

bool ButtonConnect::connected() {
    return _state == State::CONNECTED &&
           WiFi.status() == WL_CONNECTED && _ws.isConnected() && _mqtt.isConnected();
}

bool ButtonConnect::emitEvent(const char* type, const char* gesture) {
    if (!connected()) return false;
    String p = String("{\"type\":\"") + type + "\"";
    if (gesture) p += String(",\"gesture\":\"") + gesture + "\"";
    p += ",\"seq\":" + String(++_seq) + "}";
    return _mqtt.publish(_topicEvent.c_str(), p.c_str());
}

bool ButtonConnect::emitButtonPress(int clicks, int longs) {
    if (!connected()) return false;
    String p = String("{\"type\":\"button.pressed\",\"clicks\":") + clicks +
               ",\"longs\":" + longs + ",\"seq\":" + String(++_seq) + "}";
    return _mqtt.publish(_topicEvent.c_str(), p.c_str());
}

bool ButtonConnect::publishTelemetry(const String& json) {
    if (!connected()) return false;
    return _mqtt.publish(_topicTelemetry.c_str(), json.c_str());
}

bool ButtonConnect::publishBattery(float volts, int percent) {
    if (!connected()) return false;
    String p = String("{\"v\":") + String(volts, 2) + ",\"pct\":" + String(percent) + "}";
    return _mqtt.publish(_topicBattery.c_str(), p.c_str());
}

void ButtonConnect::onCommandPayload(const String& payload) {
    StaticJsonDocument<BUTTON_MQTT_BUF> doc;
    if (deserializeJson(doc, payload)) return;   // ignore malformed

    // The server sends the {Method, Args} command shape (tolerate camelCase too).
    String method = doc["Method"] | "";
    if (method.length() == 0) method = doc["method"] | "";

    // Server acknowledgement of a button press: Args = [channelIndex, "on"|"off"|"ok"|"err"].
    if (method == "ButtonAck") {
        String result = doc["Args"][1] | "";
        Serial.printf("[MQTT] button ack: %s\n", result.c_str());
        if (_onAck) _onAck(result);
        return;
    }

    // Built-in management command: reboot (RemoteCmd, code 0x01).
    if (method == "RemoteCmd") {
        int code = doc["Args"][0] | 0;
        if (code == 0x01) {
            Serial.println("[MQTT] reboot command — restarting");
            delay(200);
            ESP.restart();
        }
        return;
    }

    // Everything else → the app handler (actuators). Ack only if the command carried an id.
    if (_onCommand) {
        String id = doc["id"] | "";
        String args;
        if (!doc["Args"].isNull()) serializeJson(doc["Args"], args);
        else                       serializeJson(doc["args"], args);
        _onCommand(method, args);
        if (id.length()) ack(id, "ok");
    }
}

void ButtonConnect::ack(const String& id, const char* status, const char* error) {
    if (!connected()) return;
    String p = String("{\"id\":\"") + id + "\",\"status\":\"" + status + "\"";
    if (error) p += String(",\"error\":\"") + error + "\"";
    p += "}";
    _mqtt.publish(_topicCmdAck.c_str(), p.c_str());
}
