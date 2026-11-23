/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (PROPHET 67) */
/* PROPHET 67's voices (engine.h): oscillators, mixer, filters, envelopes, poly mod, vintage and spread, the voice
 * allocation, and the render that runs them on both cores and hands the sum to the effects (fx.c).
 *
 * Rates: the oscillators, filters and envelopes per sample; pitches, cutoffs and their coefficients per chunk of CH
 * samples (0.18 ms), except where oscillator 2 modulates them through poly mod: then per sample, as on the real thing,
 * where oscillator 2 is an audio-rate source.
 *
 * The oscillators are band-limited with a polynomial BLEP applied as events: each step in a waveform (a saw's reset,
 * a pulse's two edges, a hard-sync reset) is found where it falls between two samples and smoothed over those two
 * samples (the output runs one sample late for that). Hard sync and audio-rate pulse-width modulation stay clean. */
#include "core.h"
#include "plat.h"

#define CH 8u                            /* a chunk */
#define HAAS 32u                         /* the spread's per-voice delay line (a power of two) */

/* ------------------------------------------------------------- the patch --- */
/* eng_patch's numbers (main loop writes, the render reads; each a single float, no torn values) */
static struct {
    float o1_semi, o2_semi, o1_shape, o2_shape, o1_pw, o2_pw;
    float mix1, mix2, sub, noise;
    uint8_t o2_mode;
    float cut_oct, res_k, env_oct, hp_oct, hp_k, keytrk, fvel, hp_env_oct;
    float fs, as;                        /* sustain levels */
    float lfo_hz, lfo_amt;
    uint8_t lfo_shape, lfo_dest;
    float pm_fenv, pm_osc2;              /* -1..1, squared with their sign */
    uint8_t pm_dosc, pm_dfilt;
    float glide_k, avel, bend;
    uint8_t unison;
    float vint, spread;
} P;

/* ------------------------------------------------------------ vintage, spread --- */
/* Each voice's own character. Hand-set rather than random: every column has its mean near 0 and no voice is extreme
 * in everything, so a chord stays in tune with itself and no single voice sticks out, at any VINTAGE. */
static const float V_T1[NVOICE] = {0.6f, -0.8f, 0.2f, 1.0f, -0.4f, -0.1f, -0.5f};
static const float V_T2[NVOICE] = {-0.5f, 0.3f, 0.9f, -0.2f, -1.0f, 0.7f, -0.2f};
static const float V_CUT[NVOICE] = {0.4f, -0.9f, 0.1f, 0.7f, -0.3f, 1.0f, -1.0f};
static const float V_RES[NVOICE] = {-0.3f, 0.6f, -0.8f, 0.2f, 0.9f, -0.5f, -0.1f};
static const float V_ATT[NVOICE] = {0.5f, -0.2f, 1.0f, -0.7f, 0.1f, -0.9f, 0.2f};
static const float V_DEC[NVOICE] = {-0.6f, 0.8f, -0.1f, 0.4f, -0.9f, 0.3f, 0.1f};
static const float V_REL[NVOICE] = {0.2f, -0.5f, 0.7f, -1.0f, 0.6f, 0.1f, -0.1f};
static const float V_AMP[NVOICE] = {-0.4f, 0.2f, 0.6f, -0.8f, 0.3f, -0.1f, 0.2f};
static const float V_PW[NVOICE] = {0.7f, -0.3f, -0.6f, 0.2f, 0.9f, -0.8f, -0.1f};
static const float V_SHP[NVOICE] = {-0.2f, 0.5f, 0.3f, -0.7f, 0.1f, 0.8f, -0.8f};
/* SPREAD: where each voice stands (the voices are taken in turn, so neighbours in a chord land apart), its detune
 * (cents at full), its colour (cutoff, semitones at full) */
static const float S_PAN[NVOICE] = {-0.95f, 0.9f, -0.35f, 0.45f, -0.7f, 0.75f, 0.05f};
static const float S_DET[NVOICE] = {-3.0f, 3.5f, -1.5f, 1.2f, -2.5f, 2.2f, 0.4f};
static const float S_COL[NVOICE] = {0.35f, -0.45f, 0.2f, -0.15f, 0.5f, -0.3f, 0.0f};
/* UNISON: each voice's place in the stack (times the detune) */
static const float U_DET[NVOICE] = {0.0f, -1.0f, 1.0f, -0.55f, 0.55f, -0.25f, 0.3f};

/* ------------------------------------------------------------- a voice --- */
typedef struct {
    float ph, prev;                      /* phase 0..1, the output a sample late (the BLEP's second half) */
} osc_t;
enum { ES_IDLE, ES_ATT, ES_DEC, ES_REL };
typedef struct {
    float v;
    uint8_t st;
} env_t;
typedef struct {
    int16_t id;                          /* the note's id (-1: none) */
    int8_t note;
    uint8_t gate, sus;                   /* the key held; let go but the pedal holds it */
    float vel, pitch, target;
    osc_t o1, o2;
    float flip;                          /* the sub-octave's half (0 / 1) */
    env_t fe, ae;
    float s1, s2, s3, s4;                /* the ladder */
    float h1, h2;                        /* the high-pass */
    float dr1, dr2, dt1, dt2;            /* VINTAGE's drift, now and where it heads (cents) */
    uint16_t dr_n;                       /* chunks until a new heading */
    uint32_t seed, age;
    /* a voice taken while it sounds: it fades out in STEAL samples, then plays this */
    int16_t pid;
    int8_t pnote;
    uint8_t pend;
    float pvel, fade;
    /* its character from VINTAGE and SPREAD (eng_patch) */
    float c1, c2, ccut, cres, ka_f, kd_f, kr_f, ka_a, kd_a, kr_a, camp, cpw, cshape;
    float gl, gr;
    uint8_t hd, far;                     /* the spread's delay on the far side (samples), which side (0 L, 1 R) */
    uint8_t hw;
    float hbuf[HAAS];
} voice_t;
static voice_t V[NVOICE];
#define STEAL 96.0f                      /* a stolen voice's fade, samples (2.2 ms) */

static uint32_t notes_n, steals_n, age_n, rr;
static uint8_t sustain;
static float bend_v, mod_v;
static int cc_cut = -1;
static float lvl_out;
static uint8_t duck_on;
static float duck_g = 1.0f, gain_s;

/* unison: the notes held, the last on top */
#define USTACK 16
static int16_t u_id[USTACK];
static int8_t u_note[USTACK];
static float u_vel[USTACK];
static int u_n;

/* the LFO, shared: its value per chunk of the block (worked out before the voices) */
static float lfo_ph, lfo_sh, lfo_now;
static uint32_t lfo_seed = 0x1234567u;
static float blk_lfo[NBLK / CH], blk_lamt;   /* its value per chunk; its amount with the mod wheel's */

/* ------------------------------------------------------- the events in --- */
/* notes from the main loop and the 1 kHz tick come through a queue; the render takes them at the start of a block */
enum { EV_ON, EV_OFF, EV_SUS, EV_ALLOFF, EV_PANIC };
typedef struct {
    uint8_t type;
    int8_t note;
    int16_t id;
    float vel;
} ev_t;
#define NEV 64u
static ev_t evq[NEV];
static volatile uint32_t ev_w, ev_r;

static void ev_put(int type, int id, int note, float vel)
{
    uint32_t f = plat_lock();
    if (ev_w - ev_r < NEV) {
        ev_t *e = &evq[ev_w % NEV];
        e->type = (uint8_t)type;
        e->id = (int16_t)id;
        e->note = (int8_t)note;
        e->vel = vel;
        ev_w++;
    }
    plat_unlock(f);
}

/* ------------------------------------------------------------- the patch --- */
static float env_seconds(int v) { return 0.001f * fm_exp2f((float)v * (14.2877f / 127.0f)); }   /* 1 ms .. 20 s */
static float cutoff_oct(int v) { return 4.321928f + (float)v * (9.965784f / 127.0f); }        /* log2: 20 Hz .. 20 kHz */
static float sq_sign(float x) { return x * fm_fabsf(x); }

void eng_patch(const uint8_t *p)
{
    int i;
    float a, s;
    P.o1_semi = (float)p[P_O1_FREQ] - 24.0f;
    P.o2_semi = (float)p[P_O2_FREQ] - 24.0f + bip(p[P_O2_FINE]) * 0.5f;
    P.o1_shape = u7(p[P_O1_SHAPE]);
    P.o2_shape = u7(p[P_O2_SHAPE]);
    P.o1_pw = 0.5f + 0.45f * u7(p[P_O1_PW]);
    P.o2_pw = 0.5f + 0.45f * u7(p[P_O2_PW]);
    P.mix1 = u7(p[P_MIX_O1]);
    P.mix2 = u7(p[P_MIX_O2]);
    P.sub = u7(p[P_O1_SUB]);
    P.noise = u7(p[P_NOISE]) * u7(p[P_NOISE]);
    P.o2_mode = p[P_O2_MODE];
    P.cut_oct = cutoff_oct(p[P_CUTOFF]);
    a = u7(p[P_RESO]);
    P.res_k = 4.25f * a * (0.6f + 0.4f * a);        /* self-oscillation from about 115 */
    P.env_oct = sq_sign(bip(p[P_ENV_AMT])) * 9.0f;
    P.hp_oct = 3.3219f + (float)p[P_HPF] * (9.2288f / 127.0f);   /* 10 Hz (out of the way) .. 6 kHz */
    P.hp_k = 2.0f - 1.85f * u7(p[P_HP_RESO]);
    P.keytrk = p[P_KEYTRK] == KT_FULL ? 1.0f : p[P_KEYTRK] == KT_HALF ? 0.5f : 0.0f;
    P.fvel = u7(p[P_FVEL]);
    P.hp_env_oct = sq_sign(bip(p[P_HP_ENV])) * 7.0f;
    P.fs = u7(p[P_FS]);
    P.as = u7(p[P_AS]);
    P.lfo_hz = 0.05f * fm_exp2f((float)p[P_LFO_RATE] * (9.2288f / 127.0f));   /* 0.05 .. 30 Hz */
    P.lfo_amt = u7(p[P_LFO_AMT]) * u7(p[P_LFO_AMT]);
    P.lfo_shape = p[P_LFO_SHAPE];
    P.lfo_dest = p[P_LFO_DEST];
    P.pm_fenv = sq_sign(bip(p[P_PM_FENV]));
    P.pm_osc2 = sq_sign(bip(p[P_PM_OSC2]));
    P.pm_dosc = p[P_PM_DOSC];
    P.pm_dfilt = p[P_PM_DFILT];
    P.glide_k = p[P_GLIDE] ? 1.0f - fm_expf(-(float)CH / (env_seconds(p[P_GLIDE] / 2 + 20) * 0.25f * SR)) : 1.0f;
    P.avel = u7(p[P_AVEL]);
    P.bend = (float)p[P_BEND];
    P.unison = p[P_UNISON];
    s = u7(p[P_SPREAD]);
    a = u7(p[P_VINTAGE]);
    a = a * fm_sqrtf(a);                 /* VINTAGE: gentle at first, pronounced at the top */
    P.vint = a;
    P.spread = s;
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &V[i];
        float pan = S_PAN[i] * s * (0.6f + 0.4f * s), th = (pan + 1.0f) * 0.7853982f;
        float tf = 1.0f + 0.30f * a * V_ATT[i], td = 1.0f + 0.28f * a * V_DEC[i], tr = 1.0f + 0.30f * a * V_REL[i];
        v->c1 = a * 7.0f * V_T1[i] + s * S_DET[i];                      /* cents */
        v->c2 = a * 9.0f * V_T2[i] + s * S_DET[i] * 0.8f;
        v->ccut = a * 0.32f * V_CUT[i] + s * S_COL[i] * (1.0f / 12.0f);   /* octaves */
        v->cres = 1.0f + 0.22f * a * V_RES[i];
        v->camp = 1.0f + 0.17f * a * V_AMP[i];                           /* about +-1.5 dB */
        v->cpw = 0.05f * a * V_PW[i];
        v->cshape = 0.04f * a * V_SHP[i];
        /* the envelopes: an RC's curve. Attack aims past the top (1.3) and stops at 1; decay and release fall to
         * where they go; the times are where a decay is down 40 dB */
        v->ka_f = k_of(env_seconds(p[P_FA]) * tf * 0.6824f);
        v->kd_f = k_of(env_seconds(p[P_FD]) * td * 0.2171f);
        v->kr_f = k_of(env_seconds(p[P_FR]) * tr * 0.2171f);
        v->ka_a = k_of(env_seconds(p[P_AA]) * tf * 0.6824f);
        v->kd_a = k_of(env_seconds(p[P_AD]) * td * 0.2171f);
        v->kr_a = k_of(env_seconds(p[P_AR]) * tr * 0.2171f);
        v->gl = fm_cosf(th);
        v->gr = fm_sinf(th);
        v->far = pan < 0.0f ? 1u : 0u;   /* the side away from it hears it a little later */
        v->hd = (uint8_t)(fm_fabsf(pan) * 16.0f * s);
    }
    fx_patch(p);
}

/* ------------------------------------------------------------- the voices --- */
static void env_gate(env_t *e) { e->st = ES_ATT; }   /* from where it is, as an analogue envelope */
static void env_rel(env_t *e)
{
    if (e->st != ES_IDLE)
        e->st = ES_REL;
}

static void voice_start(voice_t *v, int id, int note, float vel, int retrig)
{
    v->id = (int16_t)id;
    v->note = (int8_t)note;
    v->target = (float)note;
    if (P.glide_k >= 1.0f || v->pitch == 0.0f)   /* (with GLIDE each voice slides from its last note) */
        v->pitch = v->target;
    v->vel = vel;
    v->gate = 1;
    v->sus = 0;
    if (retrig) {
        env_gate(&v->fe);
        env_gate(&v->ae);
    }
    v->age = ++age_n;
    notes_n++;
}

static int voice_find(int id)
{
    int i;
    for (i = 0; i < NVOICE; i++)
        if (V[i].id == id && (V[i].gate || V[i].sus || V[i].pend))
            return i;
    return -1;
}

static void unison_play(int retrig)
{
    int i;
    for (i = 0; i < NVOICE; i++)
        voice_start(&V[i], u_id[u_n - 1], u_note[u_n - 1], u_vel[u_n - 1], retrig);
}

void voice_on(int id, int note, float vel)
{
    int i, best = -1;
    if (note < 0 || note > 127)
        return;
    if (P.unison) {
        int held = 0;
        for (i = 0; i < u_n; i++)
            if (u_id[i] == id)
                break;
        if (i < u_n) {                   /* the same note again: to the top */
            for (; i + 1 < u_n; i++) {
                u_id[i] = u_id[i + 1];
                u_note[i] = u_note[i + 1];
                u_vel[i] = u_vel[i + 1];
            }
            u_n--;
        }
        held = u_n > 0;
        if (u_n == USTACK) {
            for (i = 0; i + 1 < u_n; i++) {
                u_id[i] = u_id[i + 1];
                u_note[i] = u_note[i + 1];
                u_vel[i] = u_vel[i + 1];
            }
            u_n--;
        }
        u_id[u_n] = (int16_t)id;
        u_note[u_n] = (int8_t)note;
        u_vel[u_n] = vel;
        u_n++;
        unison_play(!held);              /* legato while another key is held */
        return;
    }
    i = voice_find(id);
    if (i >= 0) {                        /* struck again */
        voice_start(&V[i], id, note, vel, 1);
        return;
    }
    /* a free voice, taking them in turn (so each note meets another voice's character, and a chord spreads) */
    for (i = 0; i < NVOICE; i++) {
        int k = (int)((rr + (uint32_t)i) % NVOICE);
        if (V[k].ae.st == ES_IDLE && !V[k].pend) {
            best = k;
            break;
        }
    }
    if (best < 0) {                      /* none: the quietest of those let go */
        float lo = 2.0f;
        for (i = 0; i < NVOICE; i++)
            if (!V[i].gate && !V[i].sus && !V[i].pend && V[i].ae.v < lo) {
                lo = V[i].ae.v;
                best = i;
            }
    }
    if (best < 0) {                      /* none let go: the oldest note */
        uint32_t old = 0xFFFFFFFFu;
        for (i = 0; i < NVOICE; i++)
            if (!V[i].pend && V[i].age < old) {
                old = V[i].age;
                best = i;
            }
    }
    if (best < 0)
        return;
    rr = (uint32_t)(best + 1) % NVOICE;
    if (V[best].ae.st != ES_IDLE && V[best].ae.v > 0.002f) {   /* still sounding: out quickly, then the new note */
        V[best].pend = 1;
        V[best].pid = (int16_t)id;
        V[best].pnote = (int8_t)note;
        V[best].pvel = vel;
        V[best].gate = 0;
        V[best].id = -1;
        steals_n++;
        return;
    }
    voice_start(&V[best], id, note, vel, 1);
}

void voice_off(int id)
{
    int i;
    if (P.unison) {
        for (i = 0; i < u_n; i++)
            if (u_id[i] == id)
                break;
        if (i == u_n)
            return;
        for (; i + 1 < u_n; i++) {
            u_id[i] = u_id[i + 1];
            u_note[i] = u_note[i + 1];
            u_vel[i] = u_vel[i + 1];
        }
        u_n--;
        if (u_n > 0) {                   /* back to the note still held */
            for (i = 0; i < NVOICE; i++) {
                V[i].id = u_id[u_n - 1];
                V[i].note = u_note[u_n - 1];
                V[i].target = (float)u_note[u_n - 1];
            }
            return;
        }
        for (i = 0; i < NVOICE; i++) {
            V[i].gate = 0;
            if (sustain)
                V[i].sus = 1;
            else {
                env_rel(&V[i].fe);
                env_rel(&V[i].ae);
            }
        }
        return;
    }
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &V[i];
        if (v->pend && v->pid == id) {   /* let go before it started: it never will */
            v->pend = 0;
            continue;
        }
        if (v->id != id || !v->gate)
            continue;
        v->gate = 0;
        if (sustain) {
            v->sus = 1;
        } else {
            env_rel(&v->fe);
            env_rel(&v->ae);
        }
    }
}

static void sus_set(int on)
{
    int i;
    sustain = (uint8_t)on;
    if (on)
        return;
    for (i = 0; i < NVOICE; i++)
        if (V[i].sus) {
            V[i].sus = 0;
            env_rel(&V[i].fe);
            env_rel(&V[i].ae);
        }
}

static void all_off(void)
{
    int i;
    u_n = 0;
    for (i = 0; i < NVOICE; i++) {
        V[i].gate = V[i].sus = V[i].pend = 0;
        env_rel(&V[i].fe);
        env_rel(&V[i].ae);
    }
}

/* --------------------------------------------------------- the oscillators --- */
/* The waveform: TRIANGLE (shape 0) - SAW (0.5) - PULSE (1), crossfaded, the pulse's DC taken out. A step of height J
 * d samples before the current sample is smoothed over the sample before (prev) and this one (cur). */
typedef struct {
    float wt, ws, wp, pw, dc;
} wave_t;

static inline void wave_set(wave_t *w, float shape, float pw)
{
    shape = fm_clampf(shape, 0.0f, 1.0f);
    w->pw = fm_clampf(pw, 0.03f, 0.97f);
    if (shape < 0.5f) {
        w->ws = shape * 2.0f;
        w->wt = 1.0f - w->ws;
        w->wp = 0.0f;
    } else {
        w->wp = (shape - 0.5f) * 2.0f;
        w->ws = 1.0f - w->wp;
        w->wt = 0.0f;
    }
    w->dc = w->wp * (2.0f * w->pw - 1.0f);
}

static inline float wave_at(const wave_t *w, float ph)
{
    float y = w->ws * (2.0f * ph - 1.0f) - w->dc;
    if (w->wt > 0.0f)
        y += w->wt * (1.0f - 4.0f * fm_fabsf(ph - 0.5f));
    if (w->wp > 0.0f)
        y += ph < w->pw ? w->wp : -w->wp;
    return y;
}

/* a soft clip shaped like tanh, cheaper: exact at 0, within 2 % up to 3, then flat */
static inline float sat(float x)
{
    float x2;
    if (x > 3.0f)
        return 1.0f;
    if (x < -3.0f)
        return -1.0f;
    x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

#define BLEP(J, d)                                                                                                      \
    do {                                                                                                                \
        float d_ = (d), e_ = 1.0f - d_;                                                                                 \
        pc += (J) * d_ * d_ * 0.5f;                                                                                     \
        cc -= (J) * e_ * e_ * 0.5f;                                                                                     \
    } while (0)

/* one sample of an oscillator. reset: hard sync, d samples ago (-1: none). wrapped: set when its phase came round
 * (and how long ago, in *wd) */
static inline float osc_tick(osc_t *o, const wave_t *w, float inc, float reset, int *wrapped, float *wd)
{
    float old = o->ph, ph = old + inc, pc = 0.0f, cc = 0.0f, cur, out;
    *wrapped = 0;
    if (reset >= 0.0f) {                 /* synced: from where it got to straight back to the start */
        float pr = old + inc * (1.0f - reset);
        if (pr >= 1.0f)
            pr -= 1.0f;
        BLEP(wave_at(w, 0.0f) - wave_at(w, pr), reset);
        ph = inc * reset;
        *wrapped = 1;
        *wd = reset;
    } else {
        if (ph >= 1.0f) {
            ph -= 1.0f;
            *wrapped = 1;
            *wd = ph / inc;
            if (*wd > 0.999f)
                *wd = 0.999f;
            BLEP(2.0f * w->wp - 2.0f * w->ws, *wd);     /* the saw falls, the pulse rises */
            if (w->wp > 0.0f && ph >= w->pw)            /* and the pulse already fell again (very short) */
                BLEP(-2.0f * w->wp, fm_minf((ph - w->pw) / inc, 0.999f));
        } else if (w->wp > 0.0f && old < w->pw && ph >= w->pw) {
            BLEP(-2.0f * w->wp, fm_minf((ph - w->pw) / inc, 0.999f));
        }
    }
    o->ph = ph;
    cur = wave_at(w, ph);
    out = o->prev + pc;
    o->prev = cur + cc;
    return out;
}

/* ------------------------------------------------------------- the render --- */
static float vb_l[2][NBLK], vb_r[2][NBLK];   /* the voices of each core, summed */
static uint32_t blk_n;
static uint8_t blk_fm, blk_pms, blk_pmp, blk_pml, blk_pmh;   /* poly mod by oscillator 2, per sample, to ... */

static float noise_seed_f(uint32_t *s) { return rnd_f(s); }

static void voices_render(int i0, int i1, float *L, float *R)
{
    uint32_t n = blk_n, c, s, k;
    int i;
    for (s = 0; s < n; s++)
        L[s] = R[s] = 0.0f;
    for (i = i0; i < i1; i++) {
        voice_t *v = &V[i];
        if (v->ae.st == ES_IDLE && !v->pend)
            continue;
        for (c = 0; c * CH < n; c++) {
            float lfo = blk_lfo[c], ld = blk_lamt * lfo;
            float p, inc1, inc2, sh1, pw1, cut, hp, g, G, gh, kh, a1, a2, kres, comp, pm_s, pm_p, pm_l, pm_h, pm_f;
            float vel_f, famt, hamt, fen, ug, ig, G4;
            wave_t w1, w2;
            int lowf = P.o2_mode == O2M_LOW || P.o2_mode == O2M_LOWFIX;
            int sync = P.o2_mode == O2M_SYNC;
            int o2_used = (P.mix2 > 0.0f && !lowf) || sync || blk_fm || blk_pms || blk_pmp || blk_pml || blk_pmh;
            /* VINTAGE's drift: a slow wander, a new heading now and then */
            if (P.vint > 0.0f) {
                if (v->dr_n == 0) {
                    v->dt1 = rnd_f(&v->seed) * 4.5f * P.vint;
                    v->dt2 = rnd_f(&v->seed) * 4.5f * P.vint;
                    v->dr_n = (uint16_t)(1100u + (rnd_u(&v->seed) & 2047u));   /* 0.2 .. 0.57 s */
                }
                v->dr_n--;
                v->dr1 += (v->dt1 - v->dr1) * 0.0016f;
                v->dr2 += (v->dt2 - v->dr2) * 0.0016f;
            } else {
                v->dr1 = v->dr2 = 0.0f;
            }
            v->pitch += (v->target - v->pitch) * P.glide_k;
            p = v->pitch + bend_v * P.bend;
            if (P.unison)
                p += U_DET[i] * (0.08f + 0.22f * P.spread);
            if (P.lfo_dest == LD_FREQ || P.lfo_dest == LD_FREQ1)
                inc1 = hz_of(p + P.o1_semi + (v->c1 + v->dr1) * 0.01f + ld * 12.0f) * (1.0f / SR);
            else
                inc1 = hz_of(p + P.o1_semi + (v->c1 + v->dr1) * 0.01f) * (1.0f / SR);
            {
                float p2 = P.o2_semi + (v->c2 + v->dr2) * 0.01f;
                if (P.lfo_dest == LD_FREQ || P.lfo_dest == LD_FREQ2)
                    p2 += ld * 12.0f;
                if (lowf)                /* LOW FREQ: an LFO's rates (1 Hz at middle C), with the keys or without */
                    inc2 = fm_exp2f((p2 + (P.o2_mode == O2M_LOW ? p - 60.0f : 0.0f)) * (1.0f / 12.0f)) * (1.0f / SR);
                else
                    inc2 = hz_of(p + p2) * (1.0f / SR);
            }
            /* poly mod by the filter envelope (this chunk) */
            fen = v->fe.v;
            sh1 = P.o1_shape + v->cshape;
            pw1 = P.o1_pw + v->cpw;
            if (P.pm_dosc & PMO_FREQ)
                inc1 *= fm_exp2f(P.pm_fenv * fen * 3.0f);   /* up to 3 octaves */
            if (P.pm_dosc & PMO_SHAPE)
                sh1 += P.pm_fenv * fen;
            if (P.pm_dosc & PMO_PW)
                pw1 += P.pm_fenv * fen * 0.45f;
            if (P.lfo_dest == LD_PW)
                pw1 += ld * 0.42f;
            inc1 = fm_minf(inc1, 0.45f);
            inc2 = fm_minf(inc2, 0.45f);
            /* the filters this chunk */
            vel_f = 1.0f - P.fvel + P.fvel * v->vel;
            famt = P.env_oct * vel_f;
            hamt = P.hp_env_oct * vel_f;
            cut = P.cut_oct + v->ccut + famt * fen + P.keytrk * (p - 60.0f) * (1.0f / 12.0f);
            hp = P.hp_oct + hamt * fen + P.keytrk * (p - 60.0f) * (1.0f / 12.0f);
            if (cc_cut >= 0)
                cut += cutoff_oct(cc_cut) - P.cut_oct;
            if (P.pm_dfilt == PMF_LPF || P.pm_dfilt == PMF_BOTH)
                cut += P.pm_fenv * fen * 7.0f;
            if (P.pm_dfilt == PMF_HPF || P.pm_dfilt == PMF_BOTH)
                hp += P.pm_fenv * fen * 5.0f;
            if (P.lfo_dest == LD_LPF)
                cut += ld * 4.0f;
            if (P.lfo_dest == LD_HPF)
                hp += ld * 3.0f;
            kres = P.res_k * v->cres;
            comp = 1.0f + kres * 0.32f;      /* some of the bass the resonance takes, back (not all: a Prophet's) */
            g = tan_w(3.1415927f / SR * fm_clampf(fm_exp2f(cut), 12.0f, 17500.0f));
            gh = tan_w(3.1415927f / SR * fm_clampf(fm_exp2f(hp), 8.0f, 12000.0f));
            kh = P.hp_k;
            a1 = 1.0f / (1.0f + gh * (gh + kh));
            a2 = gh * a1;
            G = g / (1.0f + g);
            ig = 1.0f / (1.0f + g);
            G4 = 1.0f / (1.0f + kres * G * G * G * G);
            /* poly mod by oscillator 2, per sample */
            pm_f = blk_fm ? P.pm_osc2 * 2.0f : 0.0f;           /* octaves of FM */
            pm_s = blk_pms ? P.pm_osc2 : 0.0f;
            pm_p = blk_pmp ? P.pm_osc2 * 0.45f : 0.0f;
            pm_l = blk_pml ? P.pm_osc2 * 5.0f : 0.0f;
            pm_h = blk_pmh ? P.pm_osc2 * 4.0f : 0.0f;
            wave_set(&w2, P.o2_shape, P.o2_pw);
            wave_set(&w1, sh1, pw1);
            ug = v->camp * (P.lfo_dest == LD_AMP ? 1.0f - blk_lamt * (0.5f - 0.5f * lfo) : 1.0f)
                 * (1.0f - P.avel + P.avel * v->vel);
            for (s = 0; s < CH && c * CH + s < n; s++) {
                float o1, o2, sub, x, u, S, y1, y2, y3, y4, t, hv1, hv2, hv3, out, rs, ae;
                int wr1, wr2;
                float wd1 = 0.0f, wd2 = 0.0f;
                /* the envelopes */
                if (v->fe.st == ES_ATT) {
                    v->fe.v += (1.3f - v->fe.v) * v->ka_f;
                    if (v->fe.v >= 1.0f) {
                        v->fe.v = 1.0f;
                        v->fe.st = ES_DEC;
                    }
                } else if (v->fe.st == ES_DEC) {
                    v->fe.v += (P.fs - v->fe.v) * v->kd_f;
                } else if (v->fe.st == ES_REL) {
                    v->fe.v -= v->fe.v * v->kr_f;
                    if (v->fe.v < 1e-5f) {
                        v->fe.v = 0.0f;
                        v->fe.st = ES_IDLE;
                    }
                }
                if (v->ae.st == ES_ATT) {
                    v->ae.v += (1.3f - v->ae.v) * v->ka_a;
                    if (v->ae.v >= 1.0f) {
                        v->ae.v = 1.0f;
                        v->ae.st = ES_DEC;
                    }
                } else if (v->ae.st == ES_DEC) {
                    v->ae.v += (P.as - v->ae.v) * v->kd_a;
                } else if (v->ae.st == ES_REL) {
                    v->ae.v -= v->ae.v * v->kr_a;
                    if (v->ae.v < 2e-5f) {
                        v->ae.v = 0.0f;
                        v->ae.st = ES_IDLE;
                    }
                }
                ae = v->ae.v;
                if (v->pend) {           /* taken: fading out, then the new note */
                    v->fade -= 1.0f / STEAL;
                    if (v->fade <= 0.0f) {
                        v->fade = 1.0f;
                        v->pend = 0;
                        v->fe.v = v->ae.v = 0.0f;
                        voice_start(v, v->pid, v->pnote, v->pvel, 1);
                    } else {
                        ae *= v->fade;
                    }
                }
                /* oscillator 2 first: it is poly mod's source and the sync's master */
                if (o2_used) {
                    o2 = osc_tick(&v->o2, &w2, inc2, -1.0f, &wr2, &wd2);
                } else {                 /* not heard, not used: it only runs on (free-running, as a VCO) */
                    o2 = 0.0f;
                    wr2 = 0;
                    v->o2.ph += inc2;
                    if (v->o2.ph >= 1.0f)
                        v->o2.ph -= 1.0f;
                }
                if (pm_s != 0.0f || pm_p != 0.0f)
                    wave_set(&w1, sh1 + pm_s * o2, pw1 + pm_p * o2);
                o1 = osc_tick(&v->o1, &w1, pm_f != 0.0f ? fm_minf(inc1 * fm_exp2f(pm_f * o2), 0.45f) : inc1,
                              (sync && wr2) ? wd2 : -1.0f, &wr1, &wd1);
                if (wr1)
                    v->flip = 1.0f - v->flip;
                t = (v->o1.ph + v->flip) * 0.5f;     /* the sub-octave: a triangle an octave down */
                sub = 1.0f - 4.0f * fm_fabsf(t - 0.5f);
                x = 0.5f * (P.mix1 * o1 + (lowf ? 0.0f : P.mix2 * o2) + P.sub * sub);
                if (P.noise > 0.0f)
                    x += P.noise * 0.7f * noise_seed_f(&v->seed);
                /* the high-pass (2-pole, state variable) */
                if (pm_h != 0.0f) {
                    float gs = tan_w(3.1415927f / SR * fm_clampf(fm_exp2f(hp + pm_h * o2), 8.0f, 12000.0f));
                    a1 = 1.0f / (1.0f + gs * (gs + kh));
                    a2 = gs * a1;
                    gh = gs;
                }
                hv3 = x - v->h2;
                hv1 = a1 * v->h1 + a2 * hv3;
                hv2 = v->h2 + gh * hv1;
                v->h1 = 2.0f * hv1 - v->h1;
                v->h2 = 2.0f * hv2 - v->h2;
                x = x - kh * hv1 - hv2;
                /* the low-pass: four one-poles in a loop, solved without a delay; the input saturates */
                if (pm_l != 0.0f) {
                    float gs = tan_w(3.1415927f / SR * fm_clampf(fm_exp2f(cut + pm_l * o2), 12.0f, 17500.0f));
                    ig = 1.0f / (1.0f + gs);
                    G = gs * ig;
                    G4 = 1.0f / (1.0f + kres * G * G * G * G);
                }
                {
                    float G2 = G * G;
                    S = (G2 * G * v->s1 + G2 * v->s2 + G * v->s3 + v->s4) * ig;
                    u = (x - kres * S) * G4;
                }
                u = sat(u * 1.4f) * 0.7143f;
                rs = (u - v->s1) * G;
                y1 = rs + v->s1;
                v->s1 = y1 + rs;
                rs = (y1 - v->s2) * G;
                y2 = rs + v->s2;
                v->s2 = y2 + rs;
                rs = (y2 - v->s3) * G;
                y3 = rs + v->s3;
                v->s3 = y3 + rs;
                rs = (y3 - v->s4) * G;
                y4 = rs + v->s4;
                v->s4 = y4 + rs;
                out = y4 * comp * ae * ug * 0.9f;
                /* its place: a pan, and on the far side a fraction of a millisecond late */
                k = c * CH + s;
                if (v->hd == 0) {
                    L[k] += out * v->gl;
                    R[k] += out * v->gr;
                    continue;
                }
                v->hbuf[v->hw & (HAAS - 1u)] = out;
                {
                    float late = v->hbuf[(v->hw - v->hd) & (HAAS - 1u)];
                    if (v->far) {
                        L[k] += out * v->gl;
                        R[k] += late * v->gr;
                    } else {
                        L[k] += late * v->gl;
                        R[k] += out * v->gr;
                    }
                }
                v->hw++;
            }
        }
        v->s1 = flush_dn(v->s1);
        v->s2 = flush_dn(v->s2);
        v->s3 = flush_dn(v->s3);
        v->s4 = flush_dn(v->s4);
        v->h1 = flush_dn(v->h1);
        v->h2 = flush_dn(v->h2);
    }
}

#define SPLIT 3                          /* voices 0..2 on this core, 3..6 on the second (then the effects here) */
static void job_voices(uint32_t arg)
{
    (void)arg;
    voices_render(SPLIT, NVOICE, vb_l[1], vb_r[1]);
}

static void lfo_block(uint32_t n)
{
    uint32_t c;
    float m = mod_v;
    for (c = 0; c * CH < n; c++) {
        float ph = lfo_ph, v;
        lfo_ph += P.lfo_hz * (float)CH / SR;
        if (lfo_ph >= 1.0f) {
            lfo_ph -= 1.0f;
            lfo_sh = rnd_f(&lfo_seed);
        }
        switch (P.lfo_shape) {
        case LS_SAW: v = 2.0f * ph - 1.0f; break;
        case LS_RAMP: v = 1.0f - 2.0f * ph; break;
        case LS_SQUARE: v = ph < 0.5f ? 1.0f : -1.0f; break;
        case LS_SH: v = lfo_sh; break;
        default: v = 1.0f - 4.0f * fm_fabsf(ph - 0.5f); break;
        }
        lfo_now = v;
        blk_lfo[c] = v;
    }
    blk_lamt = fm_minf(1.0f, P.lfo_amt + m * m * 0.6f);   /* the mod wheel adds to the amount, as the Prophet's */
}

void eng_render(int32_t *lr, uint32_t n, float gain)
{
    uint32_t s;
    int on2;
    float lvl = 0.0f;
    if (n > NBLK)
        n = NBLK;
    while (ev_r != ev_w) {               /* the notes that came */
        ev_t e = evq[ev_r % NEV];
        ev_r++;
        switch (e.type) {
        case EV_ON: voice_on(e.id, e.note, e.vel); break;
        case EV_OFF: voice_off(e.id); break;
        case EV_SUS: sus_set(e.note); break;
        case EV_ALLOFF: all_off(); break;
        default: {
            int i;
            all_off();
            for (i = 0; i < NVOICE; i++) {
                V[i].ae.v = V[i].fe.v = 0.0f;
                V[i].ae.st = V[i].fe.st = ES_IDLE;
                V[i].s1 = V[i].s2 = V[i].s3 = V[i].s4 = V[i].h1 = V[i].h2 = 0.0f;
            }
            fx_clear();
            clock_panic();
        }
        }
    }
    clock_block(n);                      /* the sequencer's and the arp's notes for this block */
    blk_n = n;
    lfo_block(n);
    blk_fm = (P.pm_dosc & PMO_FREQ) && P.pm_osc2 != 0.0f;
    blk_pms = (P.pm_dosc & PMO_SHAPE) && P.pm_osc2 != 0.0f;
    blk_pmp = (P.pm_dosc & PMO_PW) && P.pm_osc2 != 0.0f;
    blk_pml = (P.pm_dfilt == PMF_LPF || P.pm_dfilt == PMF_BOTH) && P.pm_osc2 != 0.0f;
    blk_pmh = (P.pm_dfilt == PMF_HPF || P.pm_dfilt == PMF_BOTH) && P.pm_osc2 != 0.0f;
    on2 = plat_cpu2_on() && plat_cpu2_run(job_voices, 0);
    voices_render(0, SPLIT, vb_l[0], vb_r[0]);
    if (!on2 || plat_cpu2_wait() != 0)
        job_voices(0);
    for (s = 0; s < n; s++) {
        vb_l[0][s] += vb_l[1][s];
        vb_r[0][s] += vb_r[1][s];
    }
    fx_process(vb_l[0], vb_r[0], n);
    for (s = 0; s < n; s++) {
        float l = vb_l[0][s], r = vb_r[0][s], a;
        /* the output: soft above -3 dB, never over */
        a = fm_fabsf(l);
        if (a > 0.7f)
            l = (l > 0.0f ? 1.0f : -1.0f) * (0.7f + 0.3f * fm_tanhf((a - 0.7f) * 3.3333f));
        a = fm_fabsf(r);
        if (a > 0.7f)
            r = (r > 0.0f ? 1.0f : -1.0f) * (0.7f + 0.3f * fm_tanhf((a - 0.7f) * 3.3333f));
        lvl = fm_maxf(lvl, fm_fabsf(l) + fm_fabsf(r));
        gain_s += (gain - gain_s) * 0.004f;
        duck_g += ((duck_on ? 0.0f : 1.0f) - duck_g) * 0.003f;
        lr[2u * s] = (int32_t)(l * gain_s * duck_g);
        lr[2u * s + 1u] = (int32_t)(r * gain_s * duck_g);
    }
    lvl_out = lvl * 0.5f > lvl_out ? lvl * 0.5f : lvl_out * 0.97f;
}

/* -------------------------------------------------------------- the calls --- */
void eng_init(void)
{
    int i;
    for (i = 0; i < NVOICE; i++) {
        V[i].id = -1;
        V[i].fade = 1.0f;
        V[i].seed = 0x9E3779B9u * (uint32_t)(i + 1);
        V[i].o1.ph = 0.13f * (float)i;   /* free-running: they start wherever */
        V[i].o2.ph = 0.37f * (float)i - (float)(int)(0.37f * (float)i);
        V[i].dr_n = (uint16_t)(i * 97);
    }
    fx_init();
    clock_init();
}

void eng_note_on(int id, int note, float vel) { ev_put(EV_ON, id, note, vel); }
void eng_note_off(int id) { ev_put(EV_OFF, id, 0, 0.0f); }
void eng_sustain(int on) { ev_put(EV_SUS, 0, on ? 1 : 0, 0.0f); }
void eng_all_off(void) { ev_put(EV_ALLOFF, 0, 0, 0.0f); }
void eng_panic(void) { ev_put(EV_PANIC, 0, 0, 0.0f); }
void eng_bend(float v) { bend_v = v; }
void eng_mod(float v) { mod_v = v; }
void eng_cutoff_cc(int v) { cc_cut = v; }
void eng_duck(int on) { duck_on = (uint8_t)(on != 0); }
int eng_ducked(void) { return duck_g < 0.001f; }

void eng_overload(void)                  /* the quietest voice let go at once */
{
    int i, q = -1;
    float lo = 2.0f;
    for (i = 0; i < NVOICE; i++)
        if (V[i].ae.st != ES_IDLE && !V[i].gate && V[i].ae.v < lo) {
            lo = V[i].ae.v;
            q = i;
        }
    if (q >= 0) {
        V[q].ae.v = 0.0f;
        V[q].ae.st = ES_IDLE;
    }
}

float eng_voice_env(int v) { return v >= 0 && v < NVOICE ? V[v].ae.v : 0.0f; }
int eng_voice_note(int v) { return v >= 0 && v < NVOICE && V[v].ae.st != ES_IDLE ? V[v].note : -1; }
int eng_voices(void)
{
    int i, k = 0;
    for (i = 0; i < NVOICE; i++)
        if (V[i].ae.st != ES_IDLE)
            k++;
    return k;
}
uint32_t eng_notes(void) { return notes_n; }
uint32_t eng_steals(void) { return steals_n; }
float eng_level(void) { return lvl_out; }
float eng_tail(void) { return fx_tail(); }
float eng_lfo(void) { return lfo_now; }
