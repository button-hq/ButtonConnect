#include "ButtonConnect.h"
#include <ArduinoJson.h>

// WebSocket handshake timeout (the WS transport must be up before MQTT CONNECT can ride it).
#define BUTTON_WS_CONNECT_TIMEOUT_MS 15000

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
}

void ButtonConnect::connectWifi() {
    if (WiFi.status() == WL_CONNECTED) return;
    Serial.printf("[WiFi] connecting to %s ...\n", _cfg.wifiSsid);
    WiFi.begin(_cfg.wifiSsid, _cfg.wifiPassword);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
        delay(250);
        Serial.print('.');
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED)
        Serial.printf("[WiFi] connected, ip=%s\n", WiFi.localIP().toString().c_str());
    else
        Serial.println("[WiFi] connect timed out — will retry");
}

// Initialize the secure-WebSocket transport + MQTT-over-WS layer (idempotent per session).
void ButtonConnect::ensureTransport() {
    if (_transportBegun) return;

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

bool ButtonConnect::connectMqtt() {
    ensureTransport();
    Serial.printf("[MQTT] WSS connect to wss://%s:%u%s ...\n",
                  _cfg.mqttHost, _cfg.mqttPort, _cfg.mqttPath);

    // The WebSocket handshake is async — pump it until the transport is up before the
    // MQTT CONNECT can ride it.
    unsigned long t0 = millis();
    while (!_ws.isConnected() && (millis() - t0) < BUTTON_WS_CONNECT_TIMEOUT_MS) {
        _ws.loop();
        delay(10);
        yield();
    }
    if (!_ws.isConnected()) {
        Serial.println("[MQTT] WSS transport not up (handshake/timeout)");
        // Tear the WS/TLS down so its BearSSL buffer is freed — otherwise each failed
        // attempt leaks it and the next retry can OOM. Re-init on the next connect.
        _ws.disconnect();
        _transportBegun = false;
        return false;
    }

    // LWT: mark offline if the connection drops.
    _mqtt.setWill(_topicStatus.c_str(), "{\"online\":false}", false, 1);
    if (!_mqtt.connect(_cfg.deviceId, _cfg.deviceId, _cfg.deviceToken)) {
        Serial.printf("[MQTT] CONNECT rejected, rc=%d\n", (int)_mqtt.getReturnCode());
        return false;
    }
    Serial.println("[MQTT] connected");

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
    return true;
}

void ButtonConnect::loop() {
    connectWifi();
    if (WiFi.status() != WL_CONNECTED) return;

    if (!connected()) {
        unsigned long now = millis();
        if (now - _lastReconnectAttempt >= 3000) {
            _lastReconnectAttempt = now;
            connectMqtt();
        }
        // Keep pumping the WS even while MQTT is down so the handshake completes.
        _ws.loop();
        return;
    }
    _ws.loop();       // pump the WebSocket (rx + heartbeat)
    _mqtt.update();   // process MQTT + fire subscription callbacks
}

bool ButtonConnect::connected() {
    return WiFi.status() == WL_CONNECTED && _ws.isConnected() && _mqtt.isConnected();
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
