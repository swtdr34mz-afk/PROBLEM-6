/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (PROPHET 67) */
/* The engine's parts talking to each other (not the app's): the voices (engine.c), the effects (fx.c), the clock with
 * the sequencer and the arp (clock.c). Everything here runs in the render unless it says otherwise. */
#pragma once
#include <stdint.h>
#include "engine.h"
#include "fastmath.h"

#define SR ENG_SR
#define NBLK 512u                        /* frames a block at most */

/* the render's hot code, in RAM (section .c1_text: copied at boot, out of the flash cache's way; both cores run it,
 * and the two cores share one instruction cache, so code the flash would have to feed twice is fed from RAM) */
#ifdef PLAT_HOST
#define HOT
#else
#define HOT __attribute__((section(".c1_text"), noinline))
#endif
/* (code in RAM reaches the flash only through a pointer, and the flash reaches it the same way, PLAT_FAR: so what it
 * calls is inlined, always) */
#define INL static inline __attribute__((always_inline))

/* notes from inside the render (the sequencer, the arp): straight to the voices */
enum { SRC_KEY = 0x000, SRC_SEQ = 0x100, SRC_ARP = 0x200 };
void voice_on(int id, int note, float vel);
void voice_off(int id);

/* the effects: their parameters (main loop) and the block (render: the voices' sum in, the output out, in place) */
void fx_init(void);
void fx_patch(const uint8_t *p);
void fx_process(float *l, float *r, uint32_t n);
void fx_clear(void);
float fx_tail(void);
float fx_weight(void);                   /* what the effects cost now, in voices (the cores' share-out) */

/* the clock: one block's worth of ticks, steps and arp notes (before the voices render) */
void clock_init(void);
void clock_block(uint32_t n);
void clock_panic(void);
float clock_pos(void);                   /* where the clock is now, in ticks (with the fraction of the one running) */
float clock_spt(void);                   /* samples a tick now */

/* small helpers */
/* 2^x, as fastmath's fm_exp2f (the same polynomial), forced inline for the RAM code */
INL float ex2(float x)
{
    fm_bits_t b;
    float p, fl, f;
    int32_t i;
    if (x < -126.0f)
        return 0.0f;
    if (x > 127.0f)
        x = 127.0f;
    i = (int32_t)(x + 0.5f);
    fl = (float)i;
    if (fl > x + 0.5f)
        fl -= 1.0f;
    f = x - fl;
    p = 1.534580891e-04f;
    p = p * f + 1.339993158e-03f;
    p = p * f + 9.618488972e-03f;
    p = p * f + 5.550328776e-02f;
    p = p * f + 2.402264689e-01f;
    p = p * f + 6.931472057e-01f;
    p = p * f + 1.0f;
    b.f = p;
    b.i += (int32_t)fl << 23;
    return b.f;
}
INL float hz_of(float midi) { return 440.0f * ex2((midi - 69.0f) * (1.0f / 12.0f)); }
INL float k_of(float tau_s) { return tau_s <= 0.0f ? 1.0f : 1.0f - fm_expf(-1.0f / (tau_s * SR)); }
INL float u7(int v) { return (float)v * (1.0f / 127.0f); }
INL float bip(int v) { return v >= 64 ? (float)(v - 64) * (1.0f / 63.0f) : (float)(v - 64) * (1.0f / 64.0f); }
INL float flush_dn(float x) { return fm_fabsf(x) < 1e-15f ? 0.0f : x; }
/* tan(w) for w 0 .. ~1.2 (the filters' prewarped coefficient): odd series, 7 % low at the top, exact below 0.6 */
INL float tan_w(float w)
{
    float w2 = w * w;
    return w * (1.0f + w2 * (0.33333f + w2 * (0.13333f + w2 * 0.05397f)));
}
INL uint32_t rnd_u(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}
INL float rnd_f(uint32_t *s) { return (float)(int32_t)rnd_u(s) * (1.0f / 2147483648.0f); }   /* -1..1 */
