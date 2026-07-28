#include "common.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavutil/pixfmt.h>

#include "recorder.h"

struct recorder_result {
    bool called;
    bool success;
};

static void
on_ended(struct sc_recorder *recorder, bool success, void *userdata) {
    (void) recorder;
    struct recorder_result *result = userdata;
    result->called = true;
    result->success = success;
}

static void
push_packet(struct sc_packet_sink *sink, int64_t pts, bool key) {
    AVPacket *packet = av_packet_alloc();
    assert(packet);
    assert(av_new_packet(packet, 4) == 0);
    static const uint8_t payload[] = {0x10, 0x00, 0x00, 0x00};
    memcpy(packet->data, payload, sizeof(payload));
    packet->pts = pts;
    packet->dts = pts;
    if (key) {
        packet->flags |= AV_PKT_FLAG_KEY;
    }
    assert(sink->ops->push(sink, packet));
    av_packet_free(&packet);
}

static void
test_exact_report_duration_stays_in_microseconds(void) {
    static const char filename[] = "test-recorder-duration.webm";
    remove(filename);

    struct recorder_result result = {0};
    static const struct sc_recorder_callbacks cbs = {
        .on_ended = on_ended,
    };
    struct sc_recorder recorder;
    assert(sc_recorder_init(&recorder, filename, SC_RECORD_FORMAT_WEBM,
                            true, false, SC_ORIENTATION_0, &cbs, &result));
    assert(sc_recorder_start(&recorder));

    AVCodecContext *ctx = avcodec_alloc_context3(NULL);
    assert(ctx);
    ctx->codec_type = AVMEDIA_TYPE_VIDEO;
    ctx->codec_id = AV_CODEC_ID_VP8;
    ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    ctx->width = 16;
    ctx->height = 16;

    struct sc_packet_sink *sink = &recorder.video_packet_sink;
    assert(sink->ops->open(sink, ctx, NULL));
    push_packet(sink, 5000000, true);
    push_packet(sink, 6000000, false);

    // Relative to the first packet: hold the second frame through exactly 3s.
    sc_recorder_set_video_end(&recorder, 3000000);
    sink->ops->close(sink);
    sc_recorder_join(&recorder);

    assert(result.called);
    assert(result.success);
    // The muxer rescales packets (WebM uses milliseconds). This getter must
    // nevertheless stay in its documented microsecond domain.
    assert(sc_recorder_get_video_duration(&recorder) == 3000000);

    sc_recorder_destroy(&recorder);
    avcodec_free_context(&ctx);
    remove(filename);
}

int
main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    test_exact_report_duration_stays_in_microseconds();
    return 0;
}
