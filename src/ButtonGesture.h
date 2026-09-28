#pragma once
#include <stdint.h>
#include <stddef.h>

// Button-press gesture classifier: turns a stream of debounced digital-pin edges into
// click/long counts, chorded the same way the server-side trigger matrix expects
// (ButtonTrigger.Clicks / Longs — see ButtonConnect::emitButtonPress).
//
// Edge capture lives in an ISR + ring buffer (see examples/ButtonPress) because
// the SDK's one remaining blocking call — the initial TLS handshake inside loop(), ~1-3 s
// and up to ~5 s worst case — would otherwise swallow presses that land during it. The
// ISR only records (timestamp, level); ALL of the debounce/classify/chord logic lives
// here so it is identical whether it's driven live or replayed from the ring buffer, and
// so it can be unit-tested on a host without any Arduino/hardware dependency (see
// test/host/test_gesture.cpp).
//
// Shipped as a public header (<ButtonGesture.h>) rather than buried in the example: any
// sketch latching a button in an ISR needs the exact same debounce-on-timestamps logic,
// and keeping one implementation avoids every integrator re-deriving (and re-bugging) the
// chording rules themselves.
namespace buttonconnect {

// One edge: the pin level immediately after the transition, and when it happened.
struct GestureEdge {
    bool          level;     // HIGH/LOW after the transition (see activeLow below)
    unsigned long atMs;
};

// Result of feeding a completed chord to the classifier.
struct GestureResult {
    int  clicks = 0;
    int  longs  = 0;
    GestureResult() = default;
    GestureResult(int c, int l) : clicks(c), longs(l) {}
};

// Stateful classifier: feed it edges in time order via onEdge(); when a chord finishes
// (idle for gestureGapMs after the last release) isChordReady() becomes true and
// takeChord() returns + resets it. Debounce and the chord-gap timer are both measured
// against recorded edge timestamps, never wall-clock delay()/live reads, so replaying a
// backlog of edges out of a ring buffer (all older than "now") still classifies exactly
// as if it had been driven live.
class GestureClassifier {
public:
    GestureClassifier(unsigned long debounceMs = 40, unsigned long longPressMs = 500,
                       unsigned long gestureGapMs = 400, bool activeLow = true)
        : _debounceMs(debounceMs), _longPressMs(longPressMs), _gestureGapMs(gestureGapMs),
          _activeLow(activeLow) {}

    // Feed one recorded (level, timestamp) edge. `level` is the raw pin level after the
    // transition (before activeLow inversion).
    void onEdge(bool level, unsigned long atMs) {
        bool pressed = _activeLow ? !level : level;

        // Debounce: ignore an edge less than debounceMs after the last ACCEPTED edge.
        if (_haveLastAccepted && (atMs - _lastAcceptedMs) < _debounceMs) return;

        if (pressed == _stablePressed) return;  // not actually a transition we track

        _stablePressed    = pressed;
        _lastAcceptedMs    = atMs;
        _haveLastAccepted  = true;

        if (pressed) {
            _pressStartMs = atMs;
        } else {
            unsigned long held = atMs - _pressStartMs;
            if (held >= _longPressMs) _longs++; else _clicks++;
            _chordOpen   = true;
            _lastEdgeMs  = atMs;
        }
    }

    // Reconcile with the live pin level. Call once per loop() AFTER draining the ring.
    // Why: debounce drops an edge that arrives < debounceMs after the last accepted one.
    // If that dropped edge was the LAST one (a glitch shorter than the debounce, or a
    // release that bounced), no later edge will ever correct the state, and the classifier
    // would stay "pressed" forever — no chord closes and the next press is misread. If the
    // pin has disagreed with our state for at least debounceMs, accept it as an edge now.
    void sync(bool level, unsigned long nowMs) {
        bool pressed = _activeLow ? !level : level;
        if (pressed == _stablePressed) return;
        if (_haveLastAccepted && (nowMs - _lastAcceptedMs) < _debounceMs) return;
        onEdge(level, nowMs);
    }

    // Must be polled with the current time (millis()) even between edges, since a chord
    // closes on IDLE time, not on an edge.
    bool chordReady(unsigned long nowMs) const {
        return _chordOpen && !_stablePressed && (nowMs - _lastEdgeMs) >= _gestureGapMs;
    }

    GestureResult takeChord() {
        GestureResult r{_clicks, _longs};
        _clicks = 0;
        _longs  = 0;
        _chordOpen = false;
        return r;
    }

private:
    unsigned long _debounceMs;
    unsigned long _longPressMs;
    unsigned long _gestureGapMs;
    bool          _activeLow;

    bool          _stablePressed    = false;
    bool          _haveLastAccepted = false;
    unsigned long _lastAcceptedMs   = 0;
    unsigned long _pressStartMs     = 0;
    unsigned long _lastEdgeMs       = 0;
    bool          _chordOpen        = false;
    int           _clicks = 0;
    int           _longs  = 0;
};

// push() runs inside an ISR. On ESP8266 an ISR must not call code that lives in flash
// (a flash access during an interrupt can crash), and a template member is not placed in
// IRAM. Force it inline into the IRAM_ATTR ISR instead.
#if defined(__GNUC__)
  #define BUTTONCONNECT_ISR_INLINE inline __attribute__((always_inline))
#else
  #define BUTTONCONNECT_ISR_INLINE inline
#endif

// Fixed-capacity single-producer/single-consumer ring buffer of edges, sized for an ISR
// producer + loop() consumer. Overflow drops the new edge and counts it (never blocks,
// never allocates) — a chatter storm loses timing precision, not memory.
// Indices use acquire/release atomics: on dual-core ESP32 the ISR and loop() can run on
// different cores, and `volatile` alone does not order the slot write before the index
// publish. (GCC builtins, so this also compiles for the host tests.)
template <size_t N>
class EdgeRing {
public:
    // Producer side (call from the ISR only).
    BUTTONCONNECT_ISR_INLINE bool push(bool level, unsigned long atMs) {
        size_t head = __atomic_load_n(&_head, __ATOMIC_RELAXED);
        size_t next = (head + 1) % N;
        if (next == __atomic_load_n(&_tail, __ATOMIC_ACQUIRE)) {  // full
            __atomic_store_n(&_overflowCount, __atomic_load_n(&_overflowCount, __ATOMIC_RELAXED) + 1, __ATOMIC_RELAXED);
            return false;
        }
        _buf[head].level = level;
        _buf[head].atMs  = atMs;
        __atomic_store_n(&_head, next, __ATOMIC_RELEASE);
        return true;
    }

    // Consumer side (call from loop()).
    bool pop(GestureEdge& out) {
        size_t tail = __atomic_load_n(&_tail, __ATOMIC_RELAXED);
        if (tail == __atomic_load_n(&_head, __ATOMIC_ACQUIRE)) return false;  // empty
        out = _buf[tail];
        __atomic_store_n(&_tail, (tail + 1) % N, __ATOMIC_RELEASE);
        return true;
    }

    unsigned long overflowCount() const { return __atomic_load_n(&_overflowCount, __ATOMIC_RELAXED); }

private:
    GestureEdge   _buf[N];
    size_t        _head = 0;
    size_t        _tail = 0;
    unsigned long _overflowCount = 0;
};

}  // namespace buttonconnect
