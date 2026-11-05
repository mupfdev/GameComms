/* libFuzzer harness: protocol decoders (COBS, deframer, link HELLO, messages).
 * Checks memory safety and encode/decode round-trip consistency.
 * SPDX-License-Identifier: MIT */
#include "gc_codec.h"
#include "gc_link.h"
#include "gc_msg.h"
#include <stdlib.h>
#include <string.h>

static void on_frame(void *ctx, const gc_u8 *body, size_t len)
{
    gc_msg m, m2;
    gc_u8 enc[GC_LINK_MAX_MSG];
    size_t n;
    gc_hello h;
    (void)ctx;
    if (len > GC_FRAME_MAX_BODY) {
        abort();
    }
    (void)gc_link_parse_hello(body, len, &h);
    if (!gc_msg_decode(body, len, &m)) {
        return;
    }
    /* every decoded string must be NUL terminated inside its buffer */
    if (m.type == GC_MT_REGISTER && (memchr(m.u.reg.name, 0, sizeof(m.u.reg.name)) == NULL ||
                                     memchr(m.u.reg.key, 0, sizeof(m.u.reg.key)) == NULL)) {
        abort();
    }
    n = gc_msg_encode(&m, enc, sizeof(enc));
    if (n && !gc_msg_decode(enc, n, &m2)) {
        abort(); /* re-encoded message must decode */
    }
    if (n && m2.type != m.type) {
        abort();
    }
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
    gc_deframer d;
    gc_u8 out[4096], enc[4096];
    size_t n, e;

    gc_deframer_init(&d, on_frame, NULL);
    gc_deframer_feed(&d, data, size);
    on_frame(NULL, data, size > GC_FRAME_MAX_BODY ? GC_FRAME_MAX_BODY : size);

    n = gc_cobs_decode(data, size, out, sizeof(out));
    if (n != GC_COBS_ERROR && n > sizeof(out)) {
        abort();
    }
    if (size <= 2048) {
        e = gc_cobs_encode(data, size, enc, sizeof(enc));
        if (!e || memchr(enc, 0, e) != NULL) {
            abort();
        }
        n = gc_cobs_decode(enc, e, out, sizeof(out));
        if (n != size || memcmp(out, data, size) != 0) {
            abort();
        }
    }
    if (size <= GC_FRAME_MAX_BODY) {
        e = gc_frame_encode(data, size, enc, sizeof(enc));
        if (!e || e > GC_FRAME_MAX_WIRE) {
            abort();
        }
    }
    return 0;
}
