/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (PROPHET 67) */
/* The clock, the step sequencer and the arpeggiator (engine.h). They run in the render, before the voices: a block's
 * ticks are counted, steps and arp notes go straight to the voices, so they land within a block (2.9 ms) of their time
 * and never drift. The clock is 24 ticks a quarter note, from BPM, or from MIDI clock while it comes in (START, STOP
 * and CONTINUE followed). */
#include "core.h"
#include "plat.h"

static const uint8_t RATE_TICKS[NRATE] = {12, 8, 6, 4, 3};

/* ------------------------------------------------------------- the clock --- */
static float bpm = 120.0f, swing;
static float acc;                        /* samples into the current tick */
static float spt = 918.75f;              /* samples a tick */
static uint32_t ticks;                   /* since the start */
static volatile uint32_t ext_in;         /* MIDI clocks come (the 1 kHz tick), the render takes them */
static uint32_t ext_taken, ext_last_blk, blk_count;
static float ext_spt;                    /* samples between MIDI clocks, smoothed */
static float since_ext;                  /* samples since the last one */
static volatile uint8_t ext_cmd;         /* a START / STOP / CONTINUE waiting */

/* ------------------------------------------------------------ the sequencer --- */
static uint32_t beat_tick;               /* ticks since PLAY (the sequencer's and the metronome's count) */
/* four tracks, each on its own: its length, rate, transpose, mute, where it is, the notes it holds */
static const seq_t *seq;
static uint8_t playing;
static uint8_t tlen[NTRK], trate[NTRK], tmute[NTRK];   /* (clock_init) */
static int8_t ttr[NTRK];
static int16_t step[NTRK];               /* the step playing */
static uint8_t step_tick[NTRK];          /* ticks into it */
static uint8_t on_n[NTRK];               /* the step's notes sounding */
static int8_t on_note[NTRK][SEQ_POLY];
static float on_left[NTRK];              /* samples until they are let go (< 0: held) */
static float swing_wait[NTRK];           /* a swung step waits this long (-1: none waiting) */
static int16_t swing_step[NTRK];

/* -------------------------------------------------------------- the arp --- */
#define AK 16
static uint8_t arp_on_, arp_mode, arp_rate = RATE_16, arp_oct = 1, arp_hold;
static float arp_gate = 0.5f;
static int8_t ak_note[AK];               /* the keys held for it, in the order played */
static float ak_vel[AK];
static uint8_t ak_n, ak_down;            /* how many it plays, how many fingers are still down */
static uint8_t arp_tick;
static int16_t arp_idx;
static int8_t arp_last = -1, arp_dir = 1, arp_shown = -1;   /* sounding; the last played (the screen) */
static float arp_left = -1.0f;
static uint8_t arp_trk;                  /* the track it plays */
static uint32_t arp_steps_n, arp_seed = 0xA5A5F00Du;

void clock_init(void)
{
    int t;
    for (t = 0; t < NTRK; t++) {
        tlen[t] = 16;
        trate[t] = RATE_16;
        step[t] = -1;
        on_left[t] = swing_wait[t] = -1.0f;
    }
}

static void seq_release(int t)
{
    uint8_t i;
    for (i = 0; i < on_n[t]; i++)
        voice_off(SRC_SEQ | TRK_ID(t) | on_note[t][i]);
    on_n[t] = 0;
    on_left[t] = -1.0f;
}

static void arp_release(void)
{
    if (arp_last >= 0)
        voice_off(SRC_ARP | TRK_ID(arp_trk) | arp_last);
    arp_last = -1;
    arp_left = -1.0f;
}

void clock_panic(void)
{
    int t;
    for (t = 0; t < NTRK; t++) {
        on_n[t] = 0;
        on_left[t] = -1.0f;
    }
    arp_last = -1;
    arp_left = -1.0f;
}

static float spt_now(void) { return eng_clock_ext() && ext_spt > 0.0f ? ext_spt : spt; }
float clock_spt(void) { return spt_now(); }
float clock_pos(void)
{
    float f = eng_clock_ext() && ext_spt > 0.0f ? since_ext / ext_spt : acc / spt;
    return (float)(ticks % 3072u) + fm_minf(f, 0.999f);   /* (wrapped: 3072 is a whole number of every synced
                                                            * LFO's cycles, and a float keeps its fraction) */
}

/* a step of a track: its notes on, at its velocity, for its gate (a TIE holds the notes before) */
static void seq_fire(int t, int st)
{
    const step_t *sp = &seq->st[t][st];
    float len = spt_now() * (float)RATE_TICKS[trate[t]], g = sp->gate ? (float)sp->gate * 0.01f : 0.5f;
    float vel = sp->vel ? (float)sp->vel * (1.0f / 127.0f) : 100.0f / 127.0f;
    uint8_t i;
    if (tmute[t]) {
        seq_release(t);
        return;
    }
    if (sp->note[0] == SN_TIE) {
        if (on_n[t])                     /* held on through this step, for its gate */
            on_left[t] = len * g + (g >= 0.99f ? 160.0f : 0.0f);
        return;
    }
    seq_release(t);
    for (i = 0; i < SEQ_POLY; i++) {
        int note = sp->note[i];
        if (note < 12)
            continue;
        note += ttr[t];
        if (note < 0 || note > 127)
            continue;
        voice_on(SRC_SEQ | TRK_ID(t) | note, note, vel);
        on_note[t][on_n[t]++] = (int8_t)note;
    }
    on_left[t] = len * g + (g >= 0.99f ? 160.0f : 0.0f);   /* (a full gate: on until the next step takes over) */
}

/* each track's place: from the ticks since PLAY (pt), not counted on step by step: so every track stays on the bar
 * (and on the metronome) whatever its LENGTH or RATE, and whenever they are changed while it plays */
static void seq_tick(uint32_t pt)
{
    int t;
    if (!playing || !seq)
        return;
    for (t = 0; t < NTRK; t++) {
        uint32_t rt = RATE_TICKS[trate[t]];
        step_tick[t] = (uint8_t)(pt % rt);
        if (step_tick[t] == 0u) {
            step[t] = (int16_t)((pt / rt) % tlen[t]);
            if ((step[t] & 1) && swing > 0.0f) {   /* the odd steps a little late */
                swing_wait[t] = swing * spt_now() * (float)RATE_TICKS[trate[t]];
                swing_step[t] = step[t];
            } else {
                seq_fire(t, step[t]);
            }
        }
    }
}

/* ---------------------------------------------------------------- the arp --- */
static int arp_order(int8_t *out)        /* the notes it walks, one octave (sorted, or as played) */
{
    int i, j, n = ak_n;
    for (i = 0; i < n; i++)
        out[i] = ak_note[i];
    if (arp_mode != ARP_PLAYED)
        for (i = 1; i < n; i++) {
            int8_t t = out[i];
            for (j = i; j > 0 && out[j - 1] > t; j--)
                out[j] = out[j - 1];
            out[j] = t;
        }
    return n;
}

static void arp_next(void)
{
    int8_t ord[AK];
    int n = arp_order(ord), total, k, note;
    float vel = 0.8f;
    arp_release();
    if (n == 0)
        return;
    total = n * arp_oct;
    switch (arp_mode) {
    case ARP_DOWN:
        arp_idx = (int16_t)(arp_idx <= 0 || arp_idx > total ? total - 1 : arp_idx - 1);
        k = arp_idx;
        break;
    case ARP_UPDOWN:
        if (total == 1) {
            k = 0;
            break;
        }
        arp_idx += arp_dir;
        if (arp_idx >= total) {
            arp_idx = (int16_t)(total - 2);
            arp_dir = -1;
        } else if (arp_idx < 0) {
            arp_idx = 1;
            arp_dir = 1;
        }
        k = arp_idx;
        break;
    case ARP_RANDOM:
        k = (int)(rnd_u(&arp_seed) % (uint32_t)total);
        break;
    default:                             /* UP, PLAYED */
        arp_idx = (int16_t)(arp_idx + 1 >= total || arp_idx < 0 ? 0 : arp_idx + 1);
        k = arp_idx;
    }
    if (k < 0 || k >= total)
        k = 0;
    note = ord[k % n] + 12 * (k / n);
    {
        int j;
        for (j = 0; j < ak_n; j++)       /* its key's velocity */
            if (ak_note[j] == ord[k % n])
                vel = ak_vel[j];
    }
    if (note > 127)
        note -= 12;
    voice_on(SRC_ARP | TRK_ID(arp_trk) | note, note, vel);
    arp_last = arp_shown = (int8_t)note;
    arp_left = spt_now() * (float)RATE_TICKS[arp_rate] * arp_gate;
    arp_steps_n++;
}

static void arp_tick_(void)
{
    if (!arp_on_)
        return;
    if (ak_n == 0) {
        if (arp_last >= 0)
            arp_release();
        arp_tick = 0;
        return;
    }
    if (++arp_tick >= RATE_TICKS[arp_rate]) {
        arp_tick = 0;
        arp_next();
    }
}

static void tick(void)
{
    ticks++;
    if (playing) {                       /* the metronome: each quarter, the bar's first higher */
        uint32_t pt = beat_tick++;
        if (pt % 24u == 0u)
            click_beat(pt % 96u == 0u);
        seq_tick(pt);
    }
    arp_tick_();
}

/* ----------------------------------------------------------- the block --- */
void clock_block(uint32_t n)
{
    float fn = (float)n;
    uint32_t in = ext_in;
    blk_count++;
    /* MIDI's START / STOP / CONTINUE */
    if (ext_cmd) {
        uint8_t c = ext_cmd;
        ext_cmd = 0;
        int t;
        if (c == CLK_STOP + 1) {
            playing = 0;
            for (t = 0; t < NTRK; t++) {
                seq_release(t);
                swing_wait[t] = -1.0f;
                step[t] = -1;
            }
        } else {
            if (c == CLK_START + 1) {
                for (t = 0; t < NTRK; t++) {
                    step[t] = -1;
                    step_tick[t] = 0;
                }
                arp_tick = 0;
                beat_tick = 0;
            }
            playing = seq != 0;
        }
    }
    if (in != ext_taken) {               /* MIDI clock: its ticks are the ticks */
        uint32_t k = in - ext_taken;
        if (k > 24u)
            k = 24u;
        ext_taken = in;
        if (since_ext > 0.0f && since_ext < SR * 0.5f) {
            float gap = since_ext / (float)k;
            ext_spt = ext_spt > 0.0f ? ext_spt + (gap - ext_spt) * 0.1f : gap;
            bpm = SR * 60.0f / (24.0f * ext_spt);
        }
        since_ext = 0.0f;
        ext_last_blk = blk_count;
        while (k--)
            tick();
    } else if (!eng_clock_ext()) {       /* our own */
        spt = SR * 60.0f / (24.0f * bpm);
        acc += fn;
        while (acc >= spt) {
            acc -= spt;
            tick();
        }
    }
    since_ext += fn;
    /* the gates */
    {
        int t;
        for (t = 0; t < NTRK; t++) {
            if (swing_wait[t] >= 0.0f) {
                swing_wait[t] -= fn;
                if (swing_wait[t] < 0.0f && playing && seq)
                    seq_fire(t, swing_step[t]);
            }
            if (on_left[t] >= 0.0f) {
                on_left[t] -= fn;
                if (on_left[t] < 0.0f)
                    seq_release(t);
            }
        }
    }
    if (arp_left >= 0.0f) {
        arp_left -= fn;
        if (arp_left < 0.0f)
            arp_release();
    }
}

/* ---------------------------------------------------------------- the calls --- */
void eng_seq_bind(const seq_t *s) { seq = s; }
void eng_trk_set(int t, int len, int r, int tr, int mute)
{
    uint32_t f;
    if (t < 0 || t >= NTRK)
        return;
    f = plat_lock();
    tlen[t] = (uint8_t)(len < 1 ? 1 : len > TRK_STEPS ? TRK_STEPS : len);
    trate[t] = (uint8_t)(r >= 0 && r < NRATE ? r : RATE_16);
    ttr[t] = (int8_t)(tr < -24 ? -24 : tr > 24 ? 24 : tr);
    if (mute && !tmute[t])
        seq_release(t);
    tmute[t] = (uint8_t)(mute != 0);
    if (step[t] >= tlen[t])
        step[t] = (int16_t)(tlen[t] - 1);
    plat_unlock(f);
}
void eng_play(int on)
{
    uint32_t f = plat_lock();
    int t;
    for (t = 0; t < NTRK; t++) {
        swing_wait[t] = -1.0f;
        seq_release(t);
        step[t] = -1;
        step_tick[t] = 0;
    }
    beat_tick = 0;
    if (on) {
        acc = spt;                       /* the first step at once */
        playing = 1;
    } else {
        playing = 0;
    }
    plat_unlock(f);
}
int eng_playing(void) { return playing; }
int eng_seq_step(int t) { return playing && t >= 0 && t < NTRK ? step[t] : -1; }
int eng_seq_nearest(int t)
{
    int st;
    if (!playing || t < 0 || t >= NTRK || (st = step[t]) < 0)
        return -1;
    if (step_tick[t] * 2 >= RATE_TICKS[trate[t]])   /* past half the step: the next one */
        st = st + 1 >= tlen[t] ? 0 : st + 1;
    return st;
}
void eng_tempo(float b, float sw)
{
    bpm = fm_clampf(b, 30.0f, 300.0f);
    swing = fm_clampf(sw, 0.0f, 0.5f);
}
float eng_bpm(void) { return bpm; }
void eng_clock(int what)
{
    if (what == CLK_TICK)
        ext_in++;
    else
        ext_cmd = (uint8_t)(what + 1);
}
int eng_clock_ext(void) { return ext_last_blk && blk_count - ext_last_blk < 172u; }   /* a clock within 0.5 s */
uint32_t eng_beats(void) { return ticks / 24u; }
float eng_clock_now(void) { return clock_pos(); }
int eng_beat(void) { return playing && beat_tick ? (int)((beat_tick - 1u) / 24u % 4u) : -1; }

void eng_arp(int on, int mode, int r, int oct, float g)
{
    uint32_t f = plat_lock();
    if (!on && arp_on_)
        arp_release();
    if (on && !arp_on_) {
        arp_tick = (uint8_t)(RATE_TICKS[r >= 0 && r < NRATE ? r : RATE_16] - 1);   /* the first note at once */
        arp_idx = -1;
        arp_dir = 1;
    }
    arp_on_ = (uint8_t)(on != 0);
    arp_mode = (uint8_t)(mode >= 0 && mode < NARP ? mode : ARP_UP);
    arp_rate = (uint8_t)(r >= 0 && r < NRATE ? r : RATE_16);
    arp_oct = (uint8_t)(oct < 1 ? 1 : oct > 4 ? 4 : oct);
    arp_gate = fm_clampf(g, 0.05f, 1.0f);
    if (!on) {
        ak_n = ak_down = 0;
    }
    plat_unlock(f);
}

void eng_arp_key(int note, int down, float vel)
{
    uint32_t f = plat_lock();
    int i;
    if (down) {
        if (ak_down == 0 && arp_hold)    /* HOLD: a new chord after every finger was off */
            ak_n = 0;
        if (ak_n == 0) {                 /* the first note: at once */
            arp_tick = (uint8_t)(RATE_TICKS[arp_rate] - 1);
            arp_idx = -1;
            arp_dir = 1;
        }
        ak_down++;
        for (i = 0; i < ak_n; i++)
            if (ak_note[i] == note)
                break;
        if (i == ak_n && ak_n < AK) {
            ak_note[ak_n] = (int8_t)note;
            ak_vel[ak_n] = vel;
            ak_n++;
        }
    } else {
        if (ak_down)
            ak_down--;
        if (!arp_hold) {
            for (i = 0; i < ak_n; i++)
                if (ak_note[i] == note) {
                    for (; i + 1 < ak_n; i++) {
                        ak_note[i] = ak_note[i + 1];
                        ak_vel[i] = ak_vel[i + 1];
                    }
                    ak_n--;
                    break;
                }
        }
    }
    plat_unlock(f);
}

void eng_arp_hold(int on)
{
    uint32_t f = plat_lock();
    arp_hold = (uint8_t)(on != 0);
    if (!on && ak_down == 0)
        ak_n = 0;
    plat_unlock(f);
}
int eng_arp_on(void) { return arp_on_; }
void eng_arp_track(int t)
{
    uint32_t f = plat_lock();
    if ((uint8_t)(t & 3) != arp_trk)
        arp_release();
    arp_trk = (uint8_t)(t & 3);
    plat_unlock(f);
}
int eng_arp_note(void) { return arp_on_ ? arp_shown : -1; }
uint32_t eng_arp_steps(void) { return arp_steps_n; }
