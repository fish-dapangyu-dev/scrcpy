#ifndef SC_DAEMON_CLIP_BUFFER_H
#define SC_DAEMON_CLIP_BUFFER_H

#include "common.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <libavcodec/avcodec.h>

#include "trait/packet_sink.h"
#include "util/thread.h"

/**
 * Clip buffer (doc/daemon.md §9.5): retain the ENCODED video stream so a
 * client can extract an arbitrary [start, end] segment of the session as a
 * standalone MP4 while recording continues.
 *
 * A packet sink attached to the video demuxer, fully independent of the
 * decoder/broadcaster/recorder sinks: each encoded packet is appended to an
 * unlinked spool file and indexed in memory {pts, offset, size, keyframe}.
 * On a "clip" request the daemon selects [last keyframe <= start, last
 * packet < end], muxes those packets into an in-memory MP4 with timestamps
 * rebased to 0, and extends the final sample duration to exactly `end`.
 * Existing packet timestamps are never moved. This represents static-screen
 * time without inventing or re-encoding frames. The live session and the
 * report recording are never touched.
 *
 * Clip times are relative to the first video packet (t = 0), the same origin
 * as recording.mp4 and the report timeline.
 */

struct sc_clip_entry {
    int64_t pts;     // µs (SCRCPY_TIME_BASE)
    uint64_t offset; // byte offset in the spool file
    uint32_t size;   // packet size in bytes
    bool key;        // keyframe
};

struct sc_clip_buffer {
    struct sc_packet_sink packet_sink; // packet sink trait

    sc_mutex mutex; // guards everything below (demuxer append vs extract)

    int fd;              // unlinked spool file, -1 when unavailable
    uint64_t spool_size; // bytes written so far

    AVCodecParameters *par; // copied from the codec context on open()

    uint8_t *config; // latest config packet (SPS/PPS / codec config record)
    size_t config_size;

    struct sc_clip_entry *entries; // sorted by pts (monotonic stream)
    size_t count;
    size_t cap;

    bool opened;
};

bool
sc_clip_buffer_init(struct sc_clip_buffer *cb);

void
sc_clip_buffer_destroy(struct sc_clip_buffer *cb);

/**
 * Last encoded packet timestamp in milliseconds relative to the first packet.
 * Unlike the session/report clock, this value may remain unchanged while the
 * screen is static. Returns false if no video packet has been received yet.
 */
bool
sc_clip_buffer_source_time_ms(struct sc_clip_buffer *cb, int64_t *out_ms);

/**
 * Pure index selection for the half-open window [start_us, end_us) (µs, same
 * scale as the entries): *begin = the keyframe at or before start_us (walking
 * forward to the first keyframe if the stream starts later), *stop = the last
 * entry with pts < end_us. Returns false when the window selects nothing.
 * Exposed for unit tests.
 */
bool
sc_clip_select(const struct sc_clip_entry *entries, size_t count,
               int64_t start_us, int64_t end_us, size_t *begin, size_t *stop);

#ifdef SC_TEST
// Expose the mux duration rule for focused unit tests.
int64_t
sc_clip_packet_duration_us(const struct sc_clip_entry *entries, size_t count,
                           size_t index, int64_t end_us);
#endif

#define SC_CLIP_ERANGE (-1)    // range not (yet) recorded
#define SC_CLIP_EINTERNAL (-2) // I/O or muxing failure

/**
 * Extract [start_ms, end_ms) as a standalone MP4. `available_end_ms` is the
 * wall-elapsed session position from sc_frame_keeper_video_time_ms(); it is
 * the sole authority for whether `end_ms` has been recorded. Encoded packet
 * PTS may stop during a static screen and must not shorten the timeline.
 *
 * On success returns 0 and sets *out (caller frees with av_free()), *out_size,
 * the actual bounds (start snaps back to a keyframe; end remains the requested
 * end), the last source packet position, and the duration for which that last
 * sample is held. On failure returns SC_CLIP_ERANGE or SC_CLIP_EINTERNAL and
 * writes a human-readable reason to errbuf.
 */
int
sc_clip_buffer_extract(struct sc_clip_buffer *cb, int64_t start_ms,
                       int64_t end_ms, int64_t available_end_ms,
                       uint8_t **out, size_t *out_size,
                       int64_t *actual_start_ms, int64_t *actual_end_ms,
                       int64_t *source_end_ms, int64_t *held_tail_ms,
                       char *errbuf, size_t errbuf_size);

#endif
