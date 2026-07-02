#include "ButtonConnect.h"
#include <ArduinoJson.h>

// SmartHome / Button root CA — pins the Button cloud broker (mq-server-01.iothub.ge).
// Bundled in the SDK so a maker doesn't have to manage certificates.
static const char BUTTON_ROOT_CA[] PROGMEM = R"CERT(
-----BEGIN CERTIFICATE-----
MIIB0DCCAXWgAwIBAgIUVTRGuw1zHrdM0v7LaSAbbGfCG/0wCgYIKoZIzj0EAwIw
PTEaMBgGA1UEAwwRU21hcnRIb21lIFJvb3QgQ0ExEjAQBgNVBAoMCVNtYXJ0SG9t
ZTELMAkGA1UEBhMCR0UwHhcNMjYwMzI0MjA0MzI2WhcNNDYwMzE5MjA0MzI2WjA9
MRowGAYDVQQDDBFTbWFydEhvbWUgUm9vdCBDQTESMBAGA1UECgwJU21hcnRIb21l
MQswCQYDVQQGEwJHRTBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IABPgUC9ZCregX
foIjOW7UwLwwM5/9NLw3hldECQa/txXhpr7mNEadopR16IKUfNagkJFvpaz8bcYY
ZFnKzopfNpejUzBRMB0GA1UdDgQWBBQ4LvxlFBSnD8BnL5VFszZ+9H/VzjAfBgNV
HSMEGDAWgBQ4LvxlFBSnD8BnL5VFszZ+9H/VzjAPBgNVHRMBAf8EBTADAQH/MAoG
CCqGSM49BAMCA0kAMEYCIQDCKQ6KrhvKrj97Lsk/RQCqUcrMb5ZHv0zeeGn1GVIY
LgIhAMYN7shMK1IkimdqWrH1FKNIE84IKRthUpq7SMOe3AoI
-----END CERTIFICATE-----
)CERT";

ButtonConnect* ButtonConnect::_self = nullptr;

void ButtonConnect::mqttTrampoline(char* topic, uint8_t* payload, unsigned int len) {
    if (_self) _self->onMqttMessage(topic, payload, len);
}

void ButtonConnect::begin(const ButtonConnectConfig& cfg) {
    _cfg = cfg;
    _self = this;

    _base           = String("byod/") + _cfg.deviceId + "/";
    _topicStatus    = _base + "status";
    _topicEvent     = _base + "event";
    _topicTelemetry = _base + "telemetry";
    _topicBattery   = _base + "battery";
    _topicCommand   = _base + "command";
    _topicCmdAck    = _base + "cmd/ack";

    // Pin the Button cloud CA on the TLS transport.
#if defined(ESP32)
    _tls.setCACert(BUTTON_ROOT_CA);
#else
    static BearSSL::X509List ca(BUTTON_ROOT_CA);
    _tls.setTrustAnchors(&ca);
#endif

    WiFi.mode(WIFI_STA);

    _mqtt.setServer(_cfg.mqttHost, _cfg.mqttPort);
    _mqtt.setCallback(&ButtonConnect::mqttTrampoline);
    _mqtt.setBufferSize(2048);   // room for inline ir.transmit timings in commands
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

bool ButtonConnect::connectMqtt() {
    Serial.printf("[MQTT] connecting to %s:%u ...\n", _cfg.mqttHost, _cfg.mqttPort);

    // LWT: mark offline if the connection drops.
    const char* will = "{\"online\":false}";
    bool ok = _mqtt.connect(_cfg.deviceId, _cfg.deviceId, _cfg.deviceToken,
                            _topicStatus.c_str(), 1, false, will);
    if (!ok) {
        Serial.printf("[MQTT] failed, rc=%d\n", _mqtt.state());
        return false;
    }
    Serial.println("[MQTT] connected");

    // Always subscribe to the command topic — even a source device receives management
    // commands (e.g. reboot). Actuation methods are dispatched to onCommand (if set).
    _mqtt.subscribe(_topicCommand.c_str());

    String status = String("{\"online\":true,\"ip\":\"") + WiFi.localIP().toString() +
                    "\",\"ssid\":\"" + WiFi.SSID() + "\",\"rssi\":" + String(WiFi.RSSI()) + "}";
    _mqtt.publish(_topicStatus.c_str(), status.c_str());
    return true;
}

void ButtonConnect::loop() {
    connectWifi();
    if (WiFi.status() != WL_CONNECTED) return;

    if (!_mqtt.connected()) {
        unsigned long now = millis();
        if (now - _lastReconnectAttempt >= 3000) {
            _lastReconnectAttempt = now;
            connectMqtt();
        }
        return;
    }
    _mqtt.loop();
}

bool ButtonConnect::connected() { return WiFi.status() == WL_CONNECTED && _mqtt.connected(); }

bool ButtonConnect::emitEvent(const char* type, const char* gesture) {
    if (!_mqtt.connected()) return false;
    String p = String("{\"type\":\"") + type + "\"";
    if (gesture) p += String(",\"gesture\":\"") + gesture + "\"";
    p += ",\"seq\":" + String(++_seq) + "}";
    return _mqtt.publish(_topicEvent.c_str(), p.c_str());
}

bool ButtonConnect::emitButtonPress(int clicks, int longs) {
    if (!_mqtt.connected()) return false;
    String p = String("{\"type\":\"button.pressed\",\"clicks\":") + clicks +
               ",\"longs\":" + longs + ",\"seq\":" + String(++_seq) + "}";
    return _mqtt.publish(_topicEvent.c_str(), p.c_str());
}

bool ButtonConnect::publishTelemetry(const String& json) {
    if (!_mqtt.connected()) return false;
    return _mqtt.publish(_topicTelemetry.c_str(), json.c_str());
}

bool ButtonConnect::publishBattery(float volts, int percent) {
    if (!_mqtt.connected()) return false;
    String p = String("{\"v\":") + String(volts, 2) + ",\"pct\":" + String(percent) + "}";
    return _mqtt.publish(_topicBattery.c_str(), p.c_str());
}

void ButtonConnect::onMqttMessage(char* topic, uint8_t* payload, unsigned int len) {
    if (_topicCommand != topic) return;

    StaticJsonDocument<2048> doc;
    if (deserializeJson(doc, payload, len)) return;   // ignore malformed

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
    if (!_mqtt.connected()) return;
    String p = String("{\"id\":\"") + id + "\",\"status\":\"" + status + "\"";
    if (error) p += String(",\"error\":\"") + error + "\"";
    p += "}";
    _mqtt.publish(_topicCmdAck.c_str(), p.c_str());
}
