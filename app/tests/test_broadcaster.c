#include "common.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>

#include "daemon/broadcaster.h"

static void
push_bytes(struct sc_packet_sink *sink, uint8_t *data, size_t size,
           int64_t pts, bool key) {
    AVPacket packet = {0};
    packet.data = data;
    packet.size = (int) size;
    packet.pts = pts;
    packet.dts = pts;
    packet.flags = key ? AV_PKT_FLAG_KEY : 0;
    assert(sink->ops->push(sink, &packet));
}

static void
test_shared_gop_bootstrap_and_overflow_recovery(void) {
    struct sc_broadcaster bc;
    assert(sc_broadcaster_init(&bc));

    AVCodecContext *ctx = avcodec_alloc_context3(NULL);
    assert(ctx);
    ctx->codec_id = AV_CODEC_ID_H264;
    ctx->width = 400;
    ctx->height = 900;
    struct sc_stream_session session = {
        .video = {
            .width = 400,
            .height = 900,
            .client_resized = true,
        },
    };
    struct sc_packet_sink *sink = &bc.packet_sink;
    assert(sink->ops->open(sink, ctx, &session));

    uint8_t config[] = {1, 2, 3};
    uint8_t key[] = {4, 5, 6};
    uint8_t delta[] = {7, 8};
    push_bytes(sink, config, sizeof(config), AV_NOPTS_VALUE, false);
    push_bytes(sink, key, sizeof(key), 0, true);
    push_bytes(sink, delta, sizeof(delta), 33333, false);

    sc_socket fake_socket = (sc_socket) 123;
    assert(sc_broadcaster_subscribe(&bc, fake_socket));
    struct sc_broadcast_subscriber *sub = &bc.subscribers[0];
    assert(sub->active);
    assert(!sub->needs_key);

    struct sc_broadcast_event *meta = sub->head;
    struct sc_broadcast_event *config_event = meta->next;
    struct sc_broadcast_event *key_event = config_event->next;
    struct sc_broadcast_event *delta_event = key_event->next;
    assert(meta->type == SC_BROADCAST_EVENT_META);
    assert(meta->client_resized);
    assert(config_event->packet == bc.config);
    assert(key_event->packet == bc.gop_head->packet);
    assert(delta_event->packet == bc.gop_tail->packet);
    assert(!delta_event->next);

    // A live delta larger than the normal queue budget overflows only this
    // subscriber. Recovery must be immediate from the shared current GOP,
    // without waiting up to one natural I-frame interval.
    size_t large_size = SC_BROADCASTER_MAX_QUEUE_BYTES + 1;
    uint8_t *large = malloc(large_size);
    assert(large);
    memset(large, 0xaa, large_size);
    push_bytes(sink, large, large_size, 66666, false);
    free(large);

    assert(!sub->needs_key);
    assert(sub->bootstrapping);
    assert(sub->queued_bytes > SC_BROADCASTER_MAX_QUEUE_BYTES);
    assert(sub->tail->packet == bc.gop_tail->packet);

    sc_broadcaster_unsubscribe(&bc, fake_socket);
    sink->ops->close(sink);
    sc_broadcaster_destroy(&bc);
    avcodec_free_context(&ctx);
}

int
main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    test_shared_gop_bootstrap_and_overflow_recovery();
    return 0;
}
