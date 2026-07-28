#include "common.h"

#include <assert.h>

#include "daemon/clip_buffer.h"

static const struct sc_clip_entry ENTRIES[] = {
    {.pts = 0,    .key = true},
    {.pts = 1000, .key = false},
    {.pts = 2000, .key = false},
    {.pts = 3000, .key = true},
    {.pts = 4000, .key = false},
};

static void
test_end_is_exclusive(void) {
    size_t begin;
    size_t stop;

    bool ok = sc_clip_select(ENTRIES, ARRAY_LEN(ENTRIES), 0, 3000,
                             &begin, &stop);
    assert(ok);
    assert(begin == 0);
    assert(stop == 2);

    ok = sc_clip_select(ENTRIES, ARRAY_LEN(ENTRIES), 0, 1, &begin, &stop);
    assert(ok);
    assert(begin == 0);
    assert(stop == 0);

    ok = sc_clip_select(ENTRIES, ARRAY_LEN(ENTRIES), 0, 0, &begin, &stop);
    assert(!ok);
}

static void
test_start_snaps_to_keyframe(void) {
    size_t begin;
    size_t stop;

    bool ok = sc_clip_select(ENTRIES, ARRAY_LEN(ENTRIES), 2500, 4500,
                             &begin, &stop);
    assert(ok);
    assert(begin == 0);
    assert(stop == 4);

    ok = sc_clip_select(ENTRIES, ARRAY_LEN(ENTRIES), 3000, 4500,
                        &begin, &stop);
    assert(ok);
    assert(begin == 3);
    assert(stop == 4);
}

static void
test_packet_durations_hold_exact_end(void) {
    assert(sc_clip_packet_duration_us(ENTRIES, 3, 0, 10000) == 1000);
    assert(sc_clip_packet_duration_us(ENTRIES, 3, 1, 10000) == 1000);
    assert(sc_clip_packet_duration_us(ENTRIES, 3, 2, 10000) == 8000);
}

int
main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    test_end_is_exclusive();
    test_start_snaps_to_keyframe();
    test_packet_durations_hold_exact_end();
    return 0;
}
