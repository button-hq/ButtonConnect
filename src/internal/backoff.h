#pragma once

// Exponential backoff with a cap, used to space out retries of any failed connect
// attempt (WiFi, NTP, WS handshake, MQTT CONNECT reject). Deliberately has NO
// dependency on Arduino.h so it can be unit-tested on the host (see
// test/host/test_backoff.cpp) — ButtonConnect.cpp is the only Arduino-facing caller.
//
// Before 0.4.0 the SDK spent 3 s fixed between every reconnect attempt forever,
// even once the broker had already told it the credentials were rejected (rc=0, e.g. a
// deleted device). That hammers the broker/DNS/TLS stack pointlessly. This ladder backs
// off 3 s → 6 → 12 → 24 → 48 → 60 (capped) and resets to 3 s the moment a connection
// actually succeeds, so a real reconnect after a brief outage is still prompt.
namespace buttonconnect {

class Backoff {
public:
    explicit Backoff(unsigned long initialMs = 3000UL, unsigned long maxMs = 60000UL)
        : _initialMs(initialMs), _maxMs(maxMs), _currentMs(initialMs) {}

    // Call once per failed attempt. Returns the delay (ms) the caller should wait
    // before its next attempt, then advances the internal delay (doubled, capped at
    // maxMs) for the failure after that.
    unsigned long onFailure() {
        unsigned long delay = _currentMs;
        unsigned long doubled = _currentMs * 2;
        // overflow-safe cap: doubled wraps to a smaller value only if _currentMs is
        // already absurdly large, which never happens with these bounds, but guard it.
        _currentMs = (doubled > _maxMs || doubled < _currentMs) ? _maxMs : doubled;
        return delay;
    }

    // Call once a connection attempt actually succeeds — the next failure starts the
    // sequence over at initialMs.
    void reset() { _currentMs = _initialMs; }

    // The delay onFailure() would return right now, without consuming it.
    unsigned long currentMs() const { return _currentMs; }

private:
    unsigned long _initialMs;
    unsigned long _maxMs;
    unsigned long _currentMs;
};

}  // namespace buttonconnect
