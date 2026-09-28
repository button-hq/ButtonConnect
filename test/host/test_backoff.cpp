// Host-side unit test for buttonconnect::Backoff (no Arduino.h dependency).
// Build/run: see README "Running the host tests".
#include <cassert>
#include <cstdio>
#include "../../src/internal/backoff.h"

using buttonconnect::Backoff;

static void test_sequence_and_cap() {
    Backoff b;  // default 3000ms initial, 60000ms cap
    unsigned long expected[] = {3000, 6000, 12000, 24000, 48000, 60000, 60000};
    for (unsigned long e : expected) {
        unsigned long got = b.onFailure();
        if (got != e) {
            printf("FAIL: expected %lu got %lu\n", e, got);
            assert(false);
        }
    }
}

static void test_reset() {
    Backoff b;
    b.onFailure(); b.onFailure(); b.onFailure();   // 3000, 6000, 12000 consumed
    assert(b.currentMs() == 24000);
    b.reset();
    assert(b.currentMs() == 3000);
    unsigned long got = b.onFailure();
    assert(got == 3000);
}

int main() {
    test_sequence_and_cap();
    test_reset();
    printf("backoff: all tests passed\n");
    return 0;
}
