/** @file gc_codec.h
 *
 *  Byte-level codecs: CRC-16, COBS, bounded buffer reader/writer and the
 *  stream framer that turns an unreliable byte stream (Bluetooth SPP,
 *  UART, TCP) into discrete, integrity-checked frames.
 *
 *  Wire format of one frame on the byte stream:
 *
 *      COBS( body || CRC16-BE(body) ) || 0x00
 *
 *  COBS guarantees that the encoded block contains no 0x00 byte, so the
 *  single 0x00 delimiter unambiguously marks a frame boundary.  A
 *  receiver that starts listening in the middle of a frame, or that
 *  sees corrupted bytes, simply discards everything up to the next
 *  0x00 and is synchronised again.  Arbitrary binary payloads are
 *  transported without escaping problems.
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#ifndef GC_CODEC_H
#define GC_CODEC_H

#include "gc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection) ---- */

gc_u16 gc_crc16(const gc_u8 *data, size_t len);
gc_u16 gc_crc16_update(gc_u16 crc, const gc_u8 *data, size_t len);

/* ---- COBS ---- */

/** Worst case encoded size of n input bytes (without delimiter). */
#define GC_COBS_MAX_ENCODED(n) ((n) + ((n) / 254) + 1)

/** Encode; returns number of bytes written or 0 if out_cap is too small. */
size_t gc_cobs_encode(const gc_u8 *in, size_t len, gc_u8 *out, size_t out_cap);

/** Decode in place capable (out may equal in); returns decoded length or
 *  (size_t)-1 on malformed input / insufficient capacity. */
size_t gc_cobs_decode(const gc_u8 *in, size_t len, gc_u8 *out, size_t out_cap);

#define GC_COBS_ERROR ((size_t)-1)

/* ---- Bounded buffer writer / reader (big-endian multi-byte fields) ---- */

typedef struct
{
    gc_u8 *data;
    size_t cap;
    size_t len;
    int overflow; /* sticky: set once any write did not fit */
} gc_writer;

void gc_w_init(gc_writer *w, gc_u8 *buf, size_t cap);
void gc_w_u8(gc_writer *w, gc_u8 v);
void gc_w_u16(gc_writer *w, gc_u16 v);
void gc_w_u32(gc_writer *w, gc_u32 v);
void gc_w_bytes(gc_writer *w, const gc_u8 *p, size_t n);
/** Length-prefixed (u8) string, truncated to 255 bytes / max_len. */
void gc_w_str8(gc_writer *w, const char *s, size_t len);

typedef struct
{
    const gc_u8 *data;
    size_t len;
    size_t pos;
    int error; /* sticky: set once any read ran past the end */
} gc_reader;

void gc_r_init(gc_reader *r, const gc_u8 *buf, size_t len);
gc_u8 gc_r_u8(gc_reader *r);
gc_u16 gc_r_u16(gc_reader *r);
gc_u32 gc_r_u32(gc_reader *r);
const gc_u8 *gc_r_bytes(gc_reader *r, size_t n);
/** Reads a u8-length-prefixed string into out (NUL terminated, truncated
 *  to out_cap-1).  Returns the stored length. */
size_t gc_r_str8(gc_reader *r, char *out, size_t out_cap);
size_t gc_r_remaining(const gc_reader *r);

/* ---- Stream framer ---- */

#ifndef GC_FRAME_MAX_BODY
#define GC_FRAME_MAX_BODY 1100 /* link header + largest message */
#endif

#define GC_FRAME_MAX_WIRE (GC_COBS_MAX_ENCODED(GC_FRAME_MAX_BODY + 2) + 2)

typedef void (*gc_frame_cb)(void *ctx, const gc_u8 *body, size_t len);

typedef struct
{
    gc_u8 buf[GC_COBS_MAX_ENCODED(GC_FRAME_MAX_BODY + 2)];
    size_t len;
    int discarding; /* oversize frame: drop until delimiter */
    gc_frame_cb cb;
    void *ctx;
    /* statistics (useful for diagnostics and tests) */
    gc_u32 frames_ok;
    gc_u32 err_crc;
    gc_u32 err_cobs;
    gc_u32 err_oversize;
    gc_u32 err_short;
} gc_deframer;

void gc_deframer_init(gc_deframer *d, gc_frame_cb cb, void *ctx);
void gc_deframer_reset(gc_deframer *d);
/** Feed any number of received bytes (partial frames are fine). */
void gc_deframer_feed(gc_deframer *d, const gc_u8 *data, size_t len);

/** Encode body into a complete wire frame including the trailing 0x00
 *  delimiter and a leading 0x00 (flushes a partial frame at the peer).
 *  Returns the number of bytes written or 0 on error. */
size_t gc_frame_encode(const gc_u8 *body, size_t len, gc_u8 *out, size_t out_cap);

#ifdef __cplusplus
}
#endif

#endif /* GC_CODEC_H */
