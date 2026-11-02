/** @file gc_types.h
 *
 *  Portable fixed-width types for the GameComms protocol library.
 *
 *  The protocol library is shared by three very different toolchains:
 *  the N-Gage SDK (GCC 2.9x, C89/C++98, no <stdint.h>), the ESP32
 *  (GCC, newlib) and the endpoint server (GCC/Clang/MSVC).  It
 *  therefore deliberately avoids <stdint.h>, dynamic memory, floating
 *  point and C99 constructs.
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#ifndef GC_TYPES_H
#define GC_TYPES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char  gc_u8;
typedef signed char    gc_i8;
typedef unsigned short gc_u16;
typedef signed short   gc_i16;
typedef unsigned int   gc_u32; /* int is 32 bits on every supported target */
typedef signed int     gc_i32;

#define GC_TRUE  1
#define GC_FALSE 0

/* Compile-time size check (C89 compatible). */
#define GC_STATIC_ASSERT(name, cond) typedef char gc_static_assert_##name[(cond) ? 1 : -1]

GC_STATIC_ASSERT(u16_is_2_bytes, sizeof(gc_u16) == 2);
GC_STATIC_ASSERT(u32_is_4_bytes, sizeof(gc_u32) == 4);

/* memmove is not exported by the Symbian 6.1 EUSER, so the library
 * carries its own (memcpy/memset are available everywhere). */
void gc_memmove(void *dst, const void *src, size_t n);

/* Wrap-around safe millisecond time comparison: true if a is before b. */
#define GC_TIME_BEFORE(a, b) ((gc_i32)((gc_u32)(a) - (gc_u32)(b)) < 0)

#ifdef __cplusplus
}
#endif

#endif /* GC_TYPES_H */
