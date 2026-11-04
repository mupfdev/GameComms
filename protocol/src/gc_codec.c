/** @file gc_codec.c
 *
 *  CRC-16, COBS, bounded reader/writer and stream framer.
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#include "gc_codec.h"
#include <string.h>

void gc_memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || n == 0) {
        return;
    }
    if (d < s) {
        while (n--) {
            *d++ = *s++;
        }
    } else {
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
}

/* ------------------------------------------------------------------ */
/* CRC-16/CCITT-FALSE                                                  */
/* ------------------------------------------------------------------ */

gc_u16 gc_crc16_update(gc_u16 crc, const gc_u8 *data, size_t len)
{
    size_t i;
    int b;

    for (i = 0; i < len; i++) {
        crc ^= (gc_u16)((gc_u16)data[i] << 8);
        for (b = 0; b < 8; b++) {
            if (crc & 0x8000) {
                crc = (gc_u16)((crc << 1) ^ 0x1021);
            } else {
                crc = (gc_u16)(crc << 1);
            }
        }
    }
    return crc;
}

gc_u16 gc_crc16(const gc_u8 *data, size_t len)
{
    return gc_crc16_update(0xFFFF, data, len);
}

/* ------------------------------------------------------------------ */
/* COBS                                                                */
/* ------------------------------------------------------------------ */

size_t gc_cobs_encode(const gc_u8 *in, size_t len, gc_u8 *out, size_t out_cap)
{
    size_t read = 0;
    size_t write = 1;
    size_t code_pos = 0;
    gc_u8 code = 1;

    if (out_cap < GC_COBS_MAX_ENCODED(len)) {
        return 0;
    }

    while (read < len) {
        if (in[read] == 0) {
            out[code_pos] = code;
            code_pos = write++;
            code = 1;
            read++;
        } else {
            out[write++] = in[read++];
            code++;
            if (code == 0xFF) {
                out[code_pos] = code;
                code_pos = write++;
                code = 1;
            }
        }
    }
    out[code_pos] = code;
    return write;
}

size_t gc_cobs_decode(const gc_u8 *in, size_t len, gc_u8 *out, size_t out_cap)
{
    size_t read = 0;
    size_t write = 0;
    gc_u8 code;
    gc_u8 i;

    while (read < len) {
        code = in[read];
        /* zero is never valid inside an encoded block; a code that
         * points beyond the block means truncation */
        if (code == 0 || read + code > len) {
            return GC_COBS_ERROR;
        }
        read++;
        for (i = 1; i < code; i++) {
            if (write >= out_cap) {
                return GC_COBS_ERROR;
            }
            out[write++] = in[read++];
        }
        if (code != 0xFF && read != len) {
            if (write >= out_cap) {
                return GC_COBS_ERROR;
            }
            out[write++] = 0;
        }
    }
    return write;
}

/* ------------------------------------------------------------------ */
/* Writer / reader                                                     */
/* ------------------------------------------------------------------ */

void gc_w_init(gc_writer *w, gc_u8 *buf, size_t cap)
{
    w->data = buf;
    w->cap = cap;
    w->len = 0;
    w->overflow = 0;
}

void gc_w_u8(gc_writer *w, gc_u8 v)
{
    if (w->len + 1 > w->cap) {
        w->overflow = 1;
        return;
    }
    w->data[w->len++] = v;
}

void gc_w_u16(gc_writer *w, gc_u16 v)
{
    gc_w_u8(w, (gc_u8)(v >> 8));
    gc_w_u8(w, (gc_u8)(v & 0xFF));
}

void gc_w_u32(gc_writer *w, gc_u32 v)
{
    gc_w_u16(w, (gc_u16)(v >> 16));
    gc_w_u16(w, (gc_u16)(v & 0xFFFF));
}

void gc_w_bytes(gc_writer *w, const gc_u8 *p, size_t n)
{
    if (n == 0) {
        return;
    }
    if (w->len + n > w->cap) {
        w->overflow = 1;
        return;
    }
    memcpy(w->data + w->len, p, n);
    w->len += n;
}

void gc_w_str8(gc_writer *w, const char *s, size_t len)
{
    if (s == NULL) {
        len = 0;
    }
    if (len > 255) {
        len = 255;
    }
    gc_w_u8(w, (gc_u8)len);
    gc_w_bytes(w, (const gc_u8 *)s, len);
}

void gc_r_init(gc_reader *r, const gc_u8 *buf, size_t len)
{
    r->data = buf;
    r->len = len;
    r->pos = 0;
    r->error = 0;
}

gc_u8 gc_r_u8(gc_reader *r)
{
    if (r->pos + 1 > r->len) {
        r->error = 1;
        r->pos = r->len;
        return 0;
    }
    return r->data[r->pos++];
}

gc_u16 gc_r_u16(gc_reader *r)
{
    gc_u16 hi = gc_r_u8(r);
    gc_u16 lo = gc_r_u8(r);
    return (gc_u16)((hi << 8) | lo);
}

gc_u32 gc_r_u32(gc_reader *r)
{
    gc_u32 hi = gc_r_u16(r);
    gc_u32 lo = gc_r_u16(r);
    return (hi << 16) | lo;
}

const gc_u8 *gc_r_bytes(gc_reader *r, size_t n)
{
    const gc_u8 *p;

    if (r->pos + n > r->len) {
        r->error = 1;
        r->pos = r->len;
        return NULL;
    }
    p = r->data + r->pos;
    r->pos += n;
    return p;
}

size_t gc_r_str8(gc_reader *r, char *out, size_t out_cap)
{
    size_t n = gc_r_u8(r);
    const gc_u8 *p = gc_r_bytes(r, n);
    size_t copy = n;

    if (out_cap == 0) {
        return 0;
    }
    if (p == NULL) {
        out[0] = '\0';
        return 0;
    }
    if (copy > out_cap - 1) {
        copy = out_cap - 1;
    }
    memcpy(out, p, copy);
    out[copy] = '\0';
    return copy;
}

size_t gc_r_remaining(const gc_reader *r)
{
    return r->len - r->pos;
}

/* ------------------------------------------------------------------ */
/* Framer                                                              */
/* ------------------------------------------------------------------ */

void gc_deframer_init(gc_deframer *d, gc_frame_cb cb, void *ctx)
{
    memset(d, 0, sizeof(*d));
    d->cb = cb;
    d->ctx = ctx;
}

void gc_deframer_reset(gc_deframer *d)
{
    d->len = 0;
    d->discarding = 0;
}

static void gc_deframer_finish(gc_deframer *d)
{
    gc_u8 *body = d->buf; /* COBS decodes safely in place */
    size_t n;
    gc_u16 crc;

    if (d->len == 0) {
        return; /* empty frame: idle delimiter, ignore */
    }
    n = gc_cobs_decode(d->buf, d->len, body, sizeof(d->buf));
    if (n == GC_COBS_ERROR) {
        d->err_cobs++;
        return;
    }
    if (n < 3) {
        d->err_short++;
        return;
    }
    crc = (gc_u16)((body[n - 2] << 8) | body[n - 1]);
    if (gc_crc16(body, n - 2) != crc) {
        d->err_crc++;
        return;
    }
    d->frames_ok++;
    if (d->cb) {
        d->cb(d->ctx, body, n - 2);
    }
}

void gc_deframer_feed(gc_deframer *d, const gc_u8 *data, size_t len)
{
    size_t i;

    for (i = 0; i < len; i++) {
        gc_u8 c = data[i];

        if (c == 0) {
            if (!d->discarding) {
                gc_deframer_finish(d);
            }
            d->len = 0;
            d->discarding = 0;
            continue;
        }
        if (d->discarding) {
            continue;
        }
        if (d->len >= sizeof(d->buf)) {
            d->err_oversize++;
            d->discarding = 1;
            d->len = 0;
            continue;
        }
        d->buf[d->len++] = c;
    }
}

/* Streaming COBS encoder used by gc_frame_encode (no large temporaries,
 * which matters on the N-Gage where the game owns a small stack). */
typedef struct
{
    gc_u8 *out;
    size_t cap;
    size_t pos;
    size_t code_pos;
    gc_u8 code;
    int err;
} gc_cobs_stream;

static void gc_cs_put(gc_cobs_stream *s, gc_u8 c)
{
    if (s->err) {
        return;
    }
    if (c == 0) {
        s->out[s->code_pos] = s->code;
        if (s->pos >= s->cap) {
            s->err = 1;
            return;
        }
        s->code_pos = s->pos++;
        s->code = 1;
        return;
    }
    if (s->pos >= s->cap) {
        s->err = 1;
        return;
    }
    s->out[s->pos++] = c;
    s->code++;
    if (s->code == 0xFF) {
        s->out[s->code_pos] = s->code;
        if (s->pos >= s->cap) {
            s->err = 1;
            return;
        }
        s->code_pos = s->pos++;
        s->code = 1;
    }
}

size_t gc_frame_encode(const gc_u8 *body, size_t len, gc_u8 *out, size_t out_cap)
{
    gc_cobs_stream s;
    gc_u16 crc;
    size_t i;

    if (len > GC_FRAME_MAX_BODY || out_cap < GC_COBS_MAX_ENCODED(len + 2) + 2) {
        return 0;
    }
    out[0] = 0;
    s.out = out + 1;
    s.cap = out_cap - 2;
    s.pos = 1;
    s.code_pos = 0;
    s.code = 1;
    s.err = 0;
    for (i = 0; i < len; i++) {
        gc_cs_put(&s, body[i]);
    }
    crc = gc_crc16(body, len);
    gc_cs_put(&s, (gc_u8)(crc >> 8));
    gc_cs_put(&s, (gc_u8)(crc & 0xFF));
    if (s.err) {
        return 0;
    }
    s.out[s.code_pos] = s.code;
    out[1 + s.pos] = 0;
    return s.pos + 2;
}
