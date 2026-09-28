// Host-side unit test for buttonconnect::GestureClassifier / EdgeRing (no Arduino.h
// dependency): 120 ms taps, double click, long hold, click+long, bounce chatter, ring
// overflow, and a release dropped by the debounce. Build/run: see README "Running the host tests".
#include <cassert>
#include <cstdio>
#include "../../src/ButtonGesture.h"

using namespace buttonconnect;

// active-low: LOW = pressed, HIGH = released, matching the example's INPUT_PULLUP wiring.
static const bool PRESSED  = false;  // LOW
static const bool RELEASED = true;   // HIGH

static GestureResult runToChord(GestureClassifier& g, unsigned long now) {
    // Advance far enough past the gesture gap that any open chord closes, then take it.
    assert(g.chordReady(now));
    return g.takeChord();
}

static void test_single_120ms_click() {
    GestureClassifier g;
    unsigned long t = 1000;
    g.onEdge(PRESSED, t);           // press at 1000
    t += 120;
    g.onEdge(RELEASED, t);          // release at 1120 → 120ms hold → click
    t += 400;                       // idle past the 400ms gesture gap
    GestureResult r = runToChord(g, t);
    assert(r.clicks == 1 && r.longs == 0);
    printf("test_single_120ms_click: OK\n");
}

static void test_double_click_200ms_apart() {
    GestureClassifier g;
    unsigned long t = 1000;
    g.onEdge(PRESSED, t);  t += 120; g.onEdge(RELEASED, t);   // click #1
    t += 200;                                                  // 200ms gap between presses
    g.onEdge(PRESSED, t);  t += 120; g.onEdge(RELEASED, t);   // click #2
    t += 400;
    GestureResult r = runToChord(g, t);
    assert(r.clicks == 2 && r.longs == 0);
    printf("test_double_click_200ms_apart: OK\n");
}

static void test_700ms_hold_is_long() {
    GestureClassifier g;
    unsigned long t = 1000;
    g.onEdge(PRESSED, t);
    t += 700;
    g.onEdge(RELEASED, t);
    t += 400;
    GestureResult r = runToChord(g, t);
    assert(r.clicks == 0 && r.longs == 1);
    printf("test_700ms_hold_is_long: OK\n");
}

static void test_click_then_long_chord() {
    GestureClassifier g;
    unsigned long t = 1000;
    g.onEdge(PRESSED, t);  t += 120; g.onEdge(RELEASED, t);   // click
    t += 300;                                                  // still inside the 400ms gap
    g.onEdge(PRESSED, t);  t += 600; g.onEdge(RELEASED, t);   // long
    t += 400;
    GestureResult r = runToChord(g, t);
    assert(r.clicks == 1 && r.longs == 1);
    printf("test_click_then_long_chord: OK\n");
}

static void test_bounce_chatter_ignored() {
    GestureClassifier g;
    unsigned long t = 1000;
    g.onEdge(PRESSED, t);
    // Chatter within the 40ms debounce window must not register as real transitions.
    g.onEdge(RELEASED, t + 5);
    g.onEdge(PRESSED, t + 10);
    g.onEdge(RELEASED, t + 15);
    g.onEdge(PRESSED, t + 20);
    // Real release well past debounce from the accepted press edge.
    t += 120;
    g.onEdge(RELEASED, t);
    t += 400;
    GestureResult r = runToChord(g, t);
    assert(r.clicks == 1 && r.longs == 0);
    printf("test_bounce_chatter_ignored: OK\n");
}

static void test_ring_overflow_counted() {
    EdgeRing<4> ring;   // capacity holds 3 usable slots (head==tail means empty)
    int pushed = 0, dropped = 0;
    for (int i = 0; i < 10; i++) {
        if (ring.push(i % 2 == 0, (unsigned long)i)) pushed++; else dropped++;
    }
    assert(pushed == 3);
    assert(dropped == 7);
    assert(ring.overflowCount() == 7);

    GestureEdge e;
    int popped = 0;
    while (ring.pop(e)) popped++;
    assert(popped == 3);
    printf("test_ring_overflow_counted: OK\n");
}

static void test_glitch_shorter_than_debounce_does_not_stick() {
    // Press edge accepted, the release comes 10 ms later (inside the 40 ms debounce) and is
    // dropped, and no further edge arrives. Without sync() the classifier stays "pressed"
    // forever. sync() with the live (released) level after the debounce must recover, and
    // the next real press must classify as a normal single click.
    GestureClassifier g;
    unsigned long t = 1000;
    g.onEdge(PRESSED, t);
    g.onEdge(RELEASED, t + 10);        // dropped by debounce
    g.sync(RELEASED, t + 20);          // still inside debounce: no change yet
    assert(!g.chordReady(t + 1000));   // stuck until reconciled...
    g.sync(RELEASED, t + 50);          // ...sync after the debounce accepts the release
    unsigned long now = t + 50 + 400;
    assert(g.chordReady(now));
    GestureResult glitch = g.takeChord();
    assert(glitch.clicks == 1 && glitch.longs == 0);   // the glitch itself reads as a click
    // A normal 120 ms press afterwards is unaffected.
    t = now + 100;
    g.onEdge(PRESSED, t); g.onEdge(RELEASED, t + 120);
    GestureResult r = runToChord(g, t + 120 + 400);
    assert(r.clicks == 1 && r.longs == 0);
    printf("test_glitch_shorter_than_debounce_does_not_stick: OK\n");
}

static void test_sync_is_a_noop_when_state_matches() {
    GestureClassifier g;
    g.sync(RELEASED, 5000);            // idle, released: nothing happens
    assert(!g.chordReady(10000));
    printf("test_sync_is_a_noop_when_state_matches: OK\n");
}

int main() {
    test_glitch_shorter_than_debounce_does_not_stick();
    test_sync_is_a_noop_when_state_matches();
    test_single_120ms_click();
    test_double_click_200ms_apart();
    test_700ms_hold_is_long();
    test_click_then_long_chord();
    test_bounce_chatter_ignored();
    test_ring_overflow_counted();
    printf("gesture: all tests passed\n");
    return 0;
}
