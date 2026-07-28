#ifndef SC_DAEMON_REPORT_H
#define SC_DAEMON_REPORT_H

#include "common.h"

#include <stdbool.h>
#include <stdint.h>

#include "daemon/frame_keeper.h"
#include "options.h"
#include "recorder.h"
#include "util/thread.h"

struct sc_clip_buffer;

/**
 * Test-report writer (DESIGN-test-report.md).
 *
 * When the daemon runs with --auto-test-report=DIR, this module:
 *  - records the device screen to a codec-compatible container via
 *    sc_recorder (recording.mp4, or recording.webm for VP8),
 *  - appends every logged client operation to DIR/events.jsonl with a
 *    frame-accurate, drift-free timestamp derived from the video PTS clock,
 *  - writes DIR/manifest.json at start and finalizes it on stop.
 *
 * Lifecycle: init() once at daemon start; start_recording()/stop_recording()
 * around the device session; log_event() from the request dispatch; destroy()
 * at daemon shutdown.
 */
struct sc_report {
    char *dir;
    char *video_path;
    const char *video_filename;
    const char *video_codec;
    const char *video_container;
    const char *video_mime_type;
    enum sc_record_format video_format;

    struct sc_frame_keeper *keeper; // not owned; source of the video dimensions
    struct sc_clip_buffer *timeline; // not owned; encoded-packet time origin

    struct sc_recorder recorder;
    // Wrapper around recorder.video_packet_sink. It captures the exact
    // session end before forwarding close(), so the recorder can extend a
    // static final frame through the report timeline.
    struct sc_packet_sink video_packet_sink;
    bool recorder_initialized;
    bool recorder_started;
    bool recorder_failed; // set from the recorder callback
    bool io_failed; // event or manifest persistence failed

    FILE *events; // events.jsonl (append)
    sc_mutex mutex; // guards events, seq, gates and failure flags
    bool accepting_events;
    uint64_t seq;

    char serial[128];
    char device_name[128];
};

/**
 * Create the report directory and open the event log. Does not start the
 * recorder (that happens per-session). Returns false on failure.
 */
bool
sc_report_init(struct sc_report *report, const char *dir,
               struct sc_frame_keeper *keeper,
               struct sc_clip_buffer *timeline, const char *serial,
               const char *device_name, enum sc_codec video_codec);

void
sc_report_destroy(struct sc_report *report);

/**
 * Initialize and start the screen recorder for the current session. On
 * success, the recorder's video packet sink must be added to the video
 * demuxer's packet source by the caller, before the demuxer starts.
 */
bool
sc_report_start_recording(struct sc_report *report, bool video,
                          enum sc_orientation orientation);

/** The recorder's video packet sink, valid after start_recording(). */
struct sc_packet_sink *
sc_report_video_sink(struct sc_report *report);

/** Whether the recorder has reported an asynchronous failure. */
bool
sc_report_failed(struct sc_report *report);

/** Mark the report incomplete after an upstream demux/sink failure. */
void
sc_report_mark_failed(struct sc_report *report);

/**
 * Stop, join and destroy the recorder (finalizes the codec-compatible video
 * file), and write the final manifest. Safe to call once after
 * start_recording().
 */
void
sc_report_stop_recording(struct sc_report *report);

/**
 * Append one operation to the event log.
 *
 * `op` is the operation name ("control", "screencap", "inject_touch", ...).
 * `action` is the optional --action text (may be NULL).
 * `extra_json` is an already-formatted JSON fragment of op-specific fields
 * without surrounding braces or a leading comma (may be NULL), e.g.
 *   "\"video_size\":{\"w\":1080,\"h\":2400},\"cmds\":[\"click 1 2\"]"
 *
 * The common fields (seq, t_ms, wall, op, action) are added automatically.
 * The timestamp is the current video position (frame-accurate).
 */
void
sc_report_log_event(struct sc_report *report, const char *op,
                    const char *action, const char *extra_json);

#endif
