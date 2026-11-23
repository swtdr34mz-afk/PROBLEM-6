/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (PROPHET 67) */
/* PROPHET 67: the panel (input, pages, knobs), the presets, the sequencer's editing, MIDI in and out, the screen (a
 * Prophet-6's front panel, drawn for 240 x 240), settings. Part of the unity build (device/unity.c, host/plat_host.c),
 * after the platform: gfx.c and announce.c are in scope. The sound is dsp/ (engine.h).
 *
 * Controls
 *   keys            play (27 keys, OCT- / OCT+ move them by octaves); with the arp on they feed the arp
 *   K1..K4          the page's four knobs
 *   SELECT          VINTAGE: each voice its own character, from clean to a well-aged poly
 *   ALGORITHM       VOICE SPREAD: the voices from the centre out into a wide stereo field
 *   PRESET          the 16 presets (turned: the next sounds at once; edits are kept only with SAVE)
 *   HOME            FILTER (CUTOFF, RESONANCE, ENV AMT, HP CUTOFF); again FILTER 2 (HP RESO, KEYBOARD, VELOCITY,
 *                   HP ENV). Held 1 s: the preset as saved (the edits dropped)
 *   SEL             OSC 1 (FREQ, SHAPE, PW, SUB OCT), again OSC 2 (FREQ, FINE, SHAPE, PW), again MIXER (OSC 1, OSC 2,
 *                   NOISE, OSC 2 mode: NORMAL, SYNC, LOW FREQ, LF FIXED)
 *   ENV             FILTER ENV (ADSR), again AMP ENV
 *   LFO             LFO 1 (RATE, AMOUNT, SHAPE, DEST), again LFO 2, again LFO SYNC (SYNC 1, SYNC 2: OFF or 4 BARS ..
 *                   1/32 to the clock, where RATE then picks the division; RETRIG 1, RETRIG 2)
 *   EDIT            POLY MOD (FILT ENV and OSC 2 amounts, bipolar; OSC 1 TO: FREQ, SHAPE, PW; FILTER: LPF, HPF)
 *   FX              REVERB (TYPE, MIX, DECAY, TONE); again SPACE REVERB (MODULATE, SHIMMER, SWELL, PREDELAY);
 *                   again CHORUS (MIX, RATE, DEPTH, DISTORT)
 *   GLO             VOICE (GLIDE, VELOCITY, UNISON, BEND); again CLOCK (BPM, SWING, KEY VEL, MIDI CH)
 *   ARP             the arp on, and its page (MODE, RATE, OCTAVES, GATE); again: off. Held 1 s: HOLD
 *   SEQ             SEQUENCER page (BARS, RATE, GATE, TRANSPOSE)
 *   PLAY            the sequencer from the top / stop
 *   REC             record: stopped, step by step (a chord at a time; PRESET moves the step, SEQ a rest, SAVE a tie);
 *                   playing, live into the nearest steps. Held 1 s: the sequence cleared
 *   SAVE            the edited preset kept in its slot; held + PRESET: into another slot (on letting go)
 *   MIDI in (USB, TRS): notes, pitch bend, CC 1 (mod wheel), 64 (sustain), 74 (cutoff), clock, START, STOP.
 *   MIDI out (USB): the keys' notes. */
#include "fastmath.h"
#include "engine.h"

/* ----------------------------------------------------------------- settings --- */
#define SET_MAGIC 0x32374850u            /* "PH72": the settings record (v0.2: LFO 2, LFO SYNC) */
#define SET_MAGIC_V1 0x31374850u         /* "PH71": v0.1's, carried over */
#define NPARAM_V1 54
#define SEQ_MAGIC 0x51534850u            /* "PHSQ" */
typedef struct {
    uint32_t magic;
    patch_t pre[NPRESETS];
    uint8_t cur;
    int8_t oct;                          /* the keys' octave -3..+3 */
    uint16_t bpm;
    uint8_t swing, kvel, mch;            /* swing 0..50 %, the keys' velocity, MIDI channel (0: all) */
    uint8_t arp_mode, arp_rate, arp_oct, arp_gate, arp_hold;
    uint8_t seq_bars, seq_rate, seq_gate;
    int8_t seq_tr;
    uint8_t rsv[3];
} settings_t;
_Static_assert(sizeof(settings_t) <= PLAT_STORE_MAX, "the settings outgrew a store object");
typedef struct {
    uint32_t magic;
    seq_t s;
} seqrec_t;
_Static_assert(sizeof(seqrec_t) <= PLAT_STORE_MAX, "the sequence outgrew a store object");

typedef struct {                         /* v0.1's record: its presets had NPARAM_V1 parameters */
    uint32_t magic;
    struct {
        char name[NAME_LEN];
        uint8_t p[NPARAM_V1];
    } pre[NPRESETS];
    uint8_t cur;
    int8_t oct;
    uint16_t bpm;
    uint8_t swing, kvel, mch;
    uint8_t arp_mode, arp_rate, arp_oct, arp_gate, arp_hold;
    uint8_t seq_bars, seq_rate, seq_gate;
    int8_t seq_tr;
    uint8_t rsv[3];
} settings_v1_t;
static settings_t set;
static seqrec_t sq;
static patch_t ed;                       /* the preset as edited (what sounds) */
static uint8_t edited;
static uint8_t set_dirty, seq_dirty;
static uint32_t dirty_at, seq_dirty_at;

static const uint8_t WK[16] = PLAT_WHITE_KEYS;
static const char *const NOTE_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const RATE_NAME[NRATE] = {"1/8", "1/8T", "1/16", "1/16T", "1/32"};
static const char *const ARP_NAME[NARP] = {"UP", "DOWN", "UP-DN", "RANDOM", "PLAYED"};
#define KEY_BASE 53                      /* the lowest key, F3, at octave 0 */
#define SRC_KEY_ID(n) (n)                /* the keys' and MIDI's notes: their ids are the notes (engine.c's SRC_KEY) */
static float u7f(int v) { return (float)v * (1.0f / 127.0f); }
static uint32_t text_len_(const char *s)  /* (no libc strlen on the device) */
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}

static void sound_globals(void)
{
    eng_tempo((float)set.bpm, (float)set.swing * 0.01f);
    eng_arp(eng_arp_on(), set.arp_mode, set.arp_rate, set.arp_oct, (float)set.arp_gate * 0.01f);
    eng_arp_hold(set.arp_hold);
    eng_seq_set(set.seq_bars, set.seq_rate, (float)set.seq_gate * 0.01f, set.seq_tr);
}

static void settings_defaults(void)
{
    uint32_t i;
    memset(&set, 0, sizeof set);
    set.magic = SET_MAGIC;
    for (i = 0; i < NPRESETS; i++)
        set.pre[i] = FACTORY[i];
    set.bpm = 110;
    set.swing = 0;
    set.kvel = 100;
    set.arp_mode = ARP_UP;
    set.arp_rate = RATE_16;
    set.arp_oct = 1;
    set.arp_gate = 50;
    set.seq_bars = 1;
    set.seq_rate = RATE_16;
    set.seq_gate = 60;
}

static int settings_ok(void)
{
    uint32_t i, k;
    if (set.magic != SET_MAGIC || set.cur >= NPRESETS || set.oct < -3 || set.oct > 3 || set.bpm < 30 || set.bpm > 300
        || set.swing > 50 || set.kvel < 1 || set.kvel > 127 || set.mch > 16 || set.arp_mode >= NARP
        || set.arp_rate >= NRATE || set.arp_oct < 1 || set.arp_oct > 4 || set.arp_gate < 5 || set.arp_gate > 100
        || set.seq_bars < 1 || set.seq_bars > SEQ_BARS || set.seq_rate >= NRATE || set.seq_gate < 5
        || set.seq_gate > 100 || set.seq_tr < -24 || set.seq_tr > 24)
        return 0;
    for (i = 0; i < NPRESETS; i++)
        for (k = 0; k < NPARAM; k++)
            if (set.pre[i].p[k] > PARAMS[k].max)
                return 0;
    return 1;
}

static int settings_v1(void)             /* v0.1's settings: the presets kept, the new parameters at their start */
{
    static settings_v1_t o;
    uint32_t i, k;
    if (plat_store_load(OBJ_SETTINGS, &o, sizeof o) != (int)sizeof o || o.magic != SET_MAGIC_V1)
        return 0;
    settings_defaults();
    for (i = 0; i < NPRESETS; i++) {
        for (k = 0; k < NAME_LEN; k++)
            set.pre[i].name[k] = o.pre[i].name[k];
        for (k = 0; k < NPARAM; k++)
            set.pre[i].p[k] = k < NPARAM_V1 ? o.pre[i].p[k] : 0;
    }
    set.cur = o.cur;
    set.oct = o.oct;
    set.bpm = o.bpm;
    set.swing = o.swing;
    set.kvel = o.kvel;
    set.mch = o.mch;
    set.arp_mode = o.arp_mode;
    set.arp_rate = o.arp_rate;
    set.arp_oct = o.arp_oct;
    set.arp_gate = o.arp_gate;
    set.arp_hold = o.arp_hold;
    set.seq_bars = o.seq_bars;
    set.seq_rate = o.seq_rate;
    set.seq_gate = o.seq_gate;
    set.seq_tr = o.seq_tr;
    if (!settings_ok()) {
        settings_defaults();
        return 0;
    }
    set_dirty = 1;                       /* (kept in the new form soon) */
    return 1;
}

static void settings_load(void)
{
    if ((plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || !settings_ok()) && !settings_v1())
        settings_defaults();
    if (plat_store_load(OBJ_SEQ, &sq, sizeof sq) != (int)sizeof sq || sq.magic != SEQ_MAGIC) {
        memset(&sq, 0, sizeof sq);
        sq.magic = SEQ_MAGIC;
    }
}

static void dirty(void)
{
    set_dirty = 1;
    dirty_at = plat_ms();
}

static void seq_changed(void)
{
    seq_dirty = 1;
    seq_dirty_at = plat_ms();
}

static void settings_save(void)
{
    if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0)
        set_dirty = 0;
}

static void seq_save(void)
{
    if (plat_store_save(OBJ_SEQ, &sq, sizeof sq) == 0)
        seq_dirty = 0;
}

static void preset_load(int i)
{
    set.cur = (uint8_t)i;
    ed = set.pre[i];
    edited = 0;
    eng_patch(ed.p);
}

/* -------------------------------------------------------------------- pages --- */
/* a knob: a parameter of the preset (0 .. NPARAM-1), or one of these (the settings, kept across presets) */
enum { G_BPM = 0x100, G_SWING, G_KVEL, G_MCH, G_ARP_MODE, G_ARP_RATE, G_ARP_OCT, G_ARP_GATE, G_SEQ_BARS, G_SEQ_RATE,
       G_SEQ_GATE, G_SEQ_TR };
enum { PG_FILTER, PG_FILTER2, PG_OSC1, PG_OSC2, PG_MIXER, PG_FENV, PG_AENV, PG_LFO, PG_POLY, PG_FXA, PG_FXB, PG_SPACE,
       PG_VOICE, PG_CLOCK, PG_ARP, PG_SEQ, PG_LFO2, PG_LSYNC, NPAGES };
typedef struct {
    const char *title;
    uint8_t button;
    uint8_t next;                        /* the same button again */
    int16_t k[4];
} page_t;
static const page_t PAGES[NPAGES] = {
    [PG_FILTER] = {"FILTER", B_HOME, PG_FILTER2, {P_CUTOFF, P_RESO, P_ENV_AMT, P_HPF}},
    [PG_FILTER2] = {"FILTER 2", B_HOME, PG_FILTER, {P_HP_RESO, P_KEYTRK, P_FVEL, P_HP_ENV}},
    [PG_OSC1] = {"OSC 1", B_SEL, PG_OSC2, {P_O1_FREQ, P_O1_SHAPE, P_O1_PW, P_O1_SUB}},
    [PG_OSC2] = {"OSC 2", B_SEL, PG_MIXER, {P_O2_FREQ, P_O2_FINE, P_O2_SHAPE, P_O2_PW}},
    [PG_MIXER] = {"MIXER", B_SEL, PG_OSC1, {P_MIX_O1, P_MIX_O2, P_NOISE, P_O2_MODE}},
    [PG_FENV] = {"FILTER ENV", B_ENV, PG_AENV, {P_FA, P_FD, P_FS, P_FR}},
    [PG_AENV] = {"AMP ENV", B_ENV, PG_FENV, {P_AA, P_AD, P_AS, P_AR}},
    [PG_LFO] = {"LFO 1", B_LFO, PG_LFO2, {P_LFO_RATE, P_LFO_AMT, P_LFO_SHAPE, P_LFO_DEST}},
    [PG_LFO2] = {"LFO 2", B_LFO, PG_LSYNC, {P_L2_RATE, P_L2_AMT, P_L2_SHAPE, P_L2_DEST}},
    [PG_LSYNC] = {"LFO SYNC", B_LFO, PG_LFO, {P_L1_SYNC, P_L2_SYNC, P_L1_RETRIG, P_L2_RETRIG}},
    [PG_POLY] = {"POLY MOD", B_EDIT, PG_POLY, {P_PM_FENV, P_PM_OSC2, P_PM_DOSC, P_PM_DFILT}},
    [PG_FXB] = {"REVERB", B_FX, PG_SPACE, {P_RV_TYPE, P_RV_MIX, P_RV_DECAY, P_RV_TONE}},
    [PG_SPACE] = {"SPACE REVERB", B_FX, PG_FXA, {P_SP_MOD, P_SP_SHIM, P_SP_SWELL, P_SP_PRE}},
    [PG_FXA] = {"CHORUS", B_FX, PG_FXB, {P_CH_MIX, P_CH_RATE, P_CH_DEPTH, P_DIST}},
    [PG_VOICE] = {"VOICE", B_GLO, PG_CLOCK, {P_GLIDE, P_AVEL, P_UNISON, P_BEND}},
    [PG_CLOCK] = {"CLOCK", B_GLO, PG_VOICE, {G_BPM, G_SWING, G_KVEL, G_MCH}},
    [PG_ARP] = {"ARPEGGIATOR", B_ARP, PG_ARP, {G_ARP_MODE, G_ARP_RATE, G_ARP_OCT, G_ARP_GATE}},
    [PG_SEQ] = {"SEQUENCER", B_SEQ, PG_SEQ, {G_SEQ_BARS, G_SEQ_RATE, G_SEQ_GATE, G_SEQ_TR}},
};

/* ----------------------------------------------------------------------- UI --- */
static struct {
    char msg[2][26];
    uint32_t msg_until;
    uint32_t keys, btn;
    uint8_t page;
    uint8_t rec;                         /* recording */
    int16_t cursor;                      /* step record: the step */
    uint32_t chord_keys;                 /* step record: the keys of the chord being gathered */
    uint8_t chord[SEQ_POLY], chord_n;
    uint8_t rec_held;                    /* step record: fingers down (keys and MIDI) */
    uint32_t home_t, rec_t, arp_t;       /* when HOME, REC, ARP went down (0: not, or used) */
    uint8_t save_down;
    int8_t copy_to;
    uint32_t enc_at[NE];
    int8_t rest[4];                      /* a bipolar knob resting at its centre: detents left */
    uint32_t turned_at, turned_what;     /* SELECT / ALGORITHM just turned: its box lit */
    uint8_t redraw;
    uint32_t drawn_at;
    int8_t intro;
    uint32_t intro_at;
    float lfo_trace[64], lfo2_trace[64];
    float tail_trace[64];
    uint8_t trace_w;
} ui;

static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}

static void int_text(int32_t v, char *b)
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}

static void note_text(int note, char *b)
{
    b[0] = 0;
    if (note < 0)
        return;
    text_cat(b, NOTE_NAME[note % 12]);
    int_text(note / 12 - 1, b + (note % 12 == 1 || note % 12 == 3 || note % 12 == 6 || note % 12 == 8 || note % 12 == 10 ? 2 : 1));
}

void app_say(const char *a, const char *b)
{
    uint32_t i;
    char t[56];
    for (i = 0; i < 25u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 25u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1400u;
    ui.redraw = 1;
    if (ann_on()) {
        t[0] = 0;
        text_cat(t, ui.msg[0]);
        if (ui.msg[1][0]) {
            text_cat(t, " ");
            text_cat(t, ui.msg[1]);
        }
        ann_say(t, 0, ANN_NOW);
    }
}

/* ------------------------------------------------------------------- knobs --- */
static int g_max(int code)
{
    switch (code) {
    case G_BPM: return 300;
    case G_SWING: return 50;
    case G_KVEL: return 127;
    case G_MCH: return 16;
    case G_ARP_MODE: return NARP - 1;
    case G_ARP_RATE: case G_SEQ_RATE: return NRATE - 1;
    case G_ARP_OCT: return 4;
    case G_ARP_GATE: case G_SEQ_GATE: return 100;
    case G_SEQ_BARS: return SEQ_BARS;
    case G_SEQ_TR: return 24;
    }
    return 127;
}
static int g_min(int code)
{
    switch (code) {
    case G_BPM: return 30;
    case G_KVEL: case G_ARP_OCT: case G_SEQ_BARS: return 1;
    case G_ARP_GATE: case G_SEQ_GATE: return 5;
    case G_SEQ_TR: return -24;
    }
    return 0;
}
static int knob_get(int code)
{
    if (code < NPARAM)
        return ed.p[code];
    switch (code) {
    case G_BPM: return set.bpm;
    case G_SWING: return set.swing;
    case G_KVEL: return set.kvel;
    case G_MCH: return set.mch;
    case G_ARP_MODE: return set.arp_mode;
    case G_ARP_RATE: return set.arp_rate;
    case G_ARP_OCT: return set.arp_oct;
    case G_ARP_GATE: return set.arp_gate;
    case G_SEQ_BARS: return set.seq_bars;
    case G_SEQ_RATE: return set.seq_rate;
    case G_SEQ_GATE: return set.seq_gate;
    case G_SEQ_TR: return set.seq_tr;
    }
    return 0;
}
static void knob_put(int code, int v)
{
    if (code < NPARAM) {
        ed.p[code] = (uint8_t)v;
        edited = 1;
        eng_patch(ed.p);
        return;
    }
    switch (code) {
    case G_BPM: set.bpm = (uint16_t)v; break;
    case G_SWING: set.swing = (uint8_t)v; break;
    case G_KVEL: set.kvel = (uint8_t)v; break;
    case G_MCH: set.mch = (uint8_t)v; break;
    case G_ARP_MODE: set.arp_mode = (uint8_t)v; break;
    case G_ARP_RATE: set.arp_rate = (uint8_t)v; break;
    case G_ARP_OCT: set.arp_oct = (uint8_t)v; break;
    case G_ARP_GATE: set.arp_gate = (uint8_t)v; break;
    case G_SEQ_BARS: set.seq_bars = (uint8_t)v; break;
    case G_SEQ_RATE: set.seq_rate = (uint8_t)v; break;
    case G_SEQ_GATE: set.seq_gate = (uint8_t)v; break;
    case G_SEQ_TR: set.seq_tr = (int8_t)v; break;
    }
    sound_globals();
    dirty();
}
static const char *knob_name(int code)
{
    static const char *const G[] = {"BPM", "SWING", "KEY VEL", "MIDI CH", "MODE", "RATE", "OCTAVES", "GATE", "BARS",
                                    "RATE", "GATE", "TRANSPOSE"};
    return code < NPARAM ? PARAMS[code].name : G[code - G_BPM];
}
static int rate_sync(int code)            /* an LFO's RATE while it is synced: its SYNC parameter (else -1) */
{
    if (code == P_LFO_RATE && ed.p[P_L1_SYNC])
        return P_L1_SYNC;
    if (code == P_L2_RATE && ed.p[P_L2_SYNC])
        return P_L2_SYNC;
    return -1;
}

static void knob_text(int code, char *b)
{
    int v = knob_get(code);
    b[0] = 0;
    if (rate_sync(code) >= 0) {
        param_text(rate_sync(code), ed.p[rate_sync(code)], b);
        return;
    }
    if (code < NPARAM) {
        param_text(code, v, b);
        return;
    }
    switch (code) {
    case G_SWING: case G_ARP_GATE: case G_SEQ_GATE:
        int_text(v, b);
        text_cat(b, "%");
        break;
    case G_MCH:
        if (v == 0)
            text_cat(b, "ALL");
        else
            int_text(v, b);
        break;
    case G_ARP_MODE: text_cat(b, ARP_NAME[v]); break;
    case G_ARP_RATE: case G_SEQ_RATE: text_cat(b, RATE_NAME[v]); break;
    case G_SEQ_TR:
        if (v > 0)
            text_cat(b, "+");
        int_text(v, b + (v > 0));
        break;
    default: int_text(v, b);
    }
}
static int knob_max(int code) { return code < NPARAM ? PARAMS[code].max : g_max(code); }
static int knob_min(int code) { return code < NPARAM ? 0 : g_min(code); }
static int knob_choice(int code)         /* one detent, one value (no acceleration) */
{
    if (code < NPARAM)
        return PARAMS[code].kind == PK_CHOICE || PARAMS[code].max <= 48;
    return code != G_BPM && code != G_KVEL && code != G_SWING && code != G_ARP_GATE && code != G_SEQ_GATE;
}
static int knob_centred(int code)        /* a bipolar knob: it rests at its centre */
{
    return code < NPARAM && (PARAMS[code].kind == PK_BIPOLAR || PARAMS[code].kind == PK_FINE);
}

/* a detent, faster when turned fast (the FM-1's encoders are small) */
static int32_t accel(int enc, int32_t e)
{
    uint32_t now = plat_ms(), dt = now - ui.enc_at[enc];
    ui.enc_at[enc] = now;
    if (dt < 35u)
        return e * 4;
    if (dt < 80u)
        return e * 2;
    return e;
}

static void knob_turn(int k, int32_t e)
{
    int code = PAGES[ui.page].k[k], v = knob_get(code), mx = knob_max(code), mn = knob_min(code), nv;
    char t[16];
    if (rate_sync(code) >= 0) {          /* synced: RATE walks the divisions (never back to OFF) */
        int sp = rate_sync(code), sv = ed.p[sp] + (e > 0 ? 1 : -1);
        if (sv >= 1 && sv < NLSY)
            knob_put(sp, sv);
        ui.redraw = 1;
        return;
    }
    if (!knob_choice(code))
        e = accel(EN_K1 + k, e);
    nv = v + (int)e;
    if (knob_centred(code)) {            /* across the centre: it stops there for a few detents */
        if (v == 64 && ui.rest[k] > 0) {
            ui.rest[k]--;
            return;
        }
        if ((v < 64 && nv >= 64) || (v > 64 && nv <= 64)) {
            nv = 64;
            ui.rest[k] = 3;
        }
    }
    if (nv < mn)
        nv = mn;
    if (nv > mx)
        nv = mx;
    if (nv == v)
        return;
    knob_put(code, nv);
    if (ann_on()) {
        knob_text(code, t);
        ann_say(knob_name(code), t, ANN_NOW);
    }
    ui.redraw = 1;
}

static void page_set(int pg)
{
    ui.page = (uint8_t)pg;
    ui.rest[0] = ui.rest[1] = ui.rest[2] = ui.rest[3] = 0;
    ui.redraw = 1;
    if (ann_on())
        ann_say(PAGES[pg].title, 0, ANN_NOW);
}

/* ------------------------------------------------------------- the sequence --- */
static int seq_len(void) { return set.seq_bars * 16; }

static void step_write(int st, const uint8_t *notes, int n)
{
    int i;
    for (i = 0; i < SEQ_POLY; i++)
        sq.s.note[st][i] = (uint8_t)(i < n ? notes[i] : SN_NONE);
    seq_changed();
}

static void step_add(int st, int note)   /* live: a note into a step (beside what is there) */
{
    uint8_t *s = sq.s.note[st];
    int i;
    if (s[0] == SN_TIE)
        s[0] = SN_NONE;
    for (i = 0; i < SEQ_POLY; i++)
        if (s[i] == note)
            return;
    for (i = 0; i < SEQ_POLY; i++)
        if (s[i] < 12) {
            s[i] = (uint8_t)note;
            seq_changed();
            return;
        }
}

static void cursor_move(int d)
{
    int n = seq_len();
    ui.cursor = (int16_t)(((ui.cursor + d) % n + n) % n);
    ui.redraw = 1;
}

/* a note down / up for the recording (keys and MIDI): live into the nearest step, or a step's chord */
static void rec_down(int note)
{
    if (!ui.rec)
        return;
    if (eng_playing()) {
        int st = eng_seq_nearest();
        if (st >= 0)
            step_add(st, note);
        return;
    }
    if (ui.rec_held == 0)
        ui.chord_n = 0;
    ui.rec_held++;
    if (ui.chord_n < SEQ_POLY)
        ui.chord[ui.chord_n++] = (uint8_t)note;
}

static void rec_up(void)
{
    if (!ui.rec || eng_playing() || ui.rec_held == 0)
        return;
    if (--ui.rec_held == 0 && ui.chord_n) {   /* every finger off: the chord into the step, on to the next */
        step_write(ui.cursor, ui.chord, ui.chord_n);
        ui.chord_n = 0;
        cursor_move(1);
    }
}

/* MIDI's notes, for the recording (the tick puts them here, the main loop takes them) */
static volatile uint8_t mrec_note[32], mrec_on[32];
static volatile uint32_t mrec_w, mrec_r;

/* ------------------------------------------------------------- the keys --- */
static int key_note(uint32_t k) { return KEY_BASE + (int)k + 12 * set.oct; }

static void note_down(int note, float vel, int from_keys)
{
    if (eng_arp_on())
        eng_arp_key(note, 1, vel);
    else
        eng_note_on(SRC_KEY_ID(note), note, vel);
    if (from_keys)
        plat_midi_out(0x09u | (0x90u << 8) | ((uint32_t)note << 16) | ((uint32_t)(vel * 127.0f) << 24));
}

static void note_up(int note, int from_keys)
{
    if (eng_arp_on())
        eng_arp_key(note, 0, 0.0f);
    else
        eng_note_off(SRC_KEY_ID(note));
    if (from_keys)
        plat_midi_out(0x08u | (0x80u << 8) | ((uint32_t)note << 16));
}

static int8_t key_sounding[NKEYS];        /* the note each key started (an octave change while held lets go of it) */

static void keys_input(uint32_t keys)
{
    uint32_t k;
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit)) {
            int n = key_note(k);
            if (n < 0 || n > 127)
                continue;
            key_sounding[k] = (int8_t)n;
            note_down(n, (float)set.kvel * (1.0f / 127.0f), 1);
            rec_down(n);
        } else if (!(keys & bit) && (ui.keys & bit)) {
            if (key_sounding[k] >= 0) {
                note_up(key_sounding[k], 1);
                rec_up();
            }
            key_sounding[k] = -1;
        }
    }
}

/* ----------------------------------------------------------------- buttons --- */
static void preset_browse(int32_t e)
{
    int c = ui.save_down && ui.copy_to >= 0 ? ui.copy_to : set.cur;
    char t[24];
    c = ((c + (e > 0 ? 1 : -1)) % NPRESETS + NPRESETS) % NPRESETS;
    if (ui.save_down) {                  /* SAVE held: where the edits go */
        ui.copy_to = (int8_t)c;
        t[0] = (char)('0' + (c + 1) / 10);
        t[1] = (char)('0' + (c + 1) % 10);
        t[2] = ' ';
        t[3] = 0;
        text_cat(t, set.pre[c].name);
        app_say("SAVE TO", t);
        return;
    }
    preset_load(c);
    dirty();
    if (ann_on())
        ann_say(ed.name, 0, ANN_NOW);
    ui.redraw = 1;
}

static void rec_toggle(void)
{
    ui.rec = (uint8_t)!ui.rec;
    ui.chord_n = ui.rec_held = 0;
    if (ui.rec) {
        if (!eng_playing()) {
            ui.cursor = 0;
            page_set(PG_SEQ);
            app_say("STEP RECORD", "PLAY A CHORD");
        } else {
            app_say("RECORD", "LIVE");
        }
    } else {
        app_say("RECORD", "OFF");
    }
}

static void button(uint32_t i)
{
    const page_t *pg = &PAGES[ui.page];
    if (ui.rec && !eng_playing()) {      /* step record: SEQ a rest, SAVE a tie */
        if (i == B_SEQ) {
            step_write(ui.cursor, 0, 0);
            cursor_move(1);
            app_say("REST", 0);
            return;
        }
        if (i == B_SAVE) {
            static const uint8_t tie[1] = {SN_TIE};
            step_write(ui.cursor, tie, 1);
            cursor_move(1);
            app_say("TIE", 0);
            return;
        }
    }
    switch (i) {
    case B_HOME:
        ui.home_t = plat_ms();
        /* fall through */
    case B_SEL: case B_ENV: case B_LFO: case B_EDIT: case B_FX: case B_GLO: case B_SEQ: {
        static const uint8_t FIRST[NB] = {[B_HOME] = PG_FILTER, [B_SEL] = PG_OSC1, [B_ENV] = PG_FENV,
                                          [B_LFO] = PG_LFO, [B_EDIT] = PG_POLY, [B_FX] = PG_FXB,
                                          [B_GLO] = PG_VOICE, [B_SEQ] = PG_SEQ};
        if (pg->button == i)
            page_set(pg->next);
        else
            page_set(FIRST[i]);
        break;
    }
    case B_ARP:
        ui.arp_t = plat_ms();
        if (ui.page == PG_ARP && eng_arp_on()) {
            eng_arp(0, set.arp_mode, set.arp_rate, set.arp_oct, (float)set.arp_gate * 0.01f);
            eng_all_off();
            page_set(PG_FILTER);
            app_say("ARP", "OFF");
        } else {
            if (!eng_arp_on()) {
                eng_all_off();
                eng_arp(1, set.arp_mode, set.arp_rate, set.arp_oct, (float)set.arp_gate * 0.01f);
                app_say("ARP", "ON");
            }
            page_set(PG_ARP);
        }
        break;
    case B_PLAY:
        eng_play(!eng_playing());
        ui.chord_n = ui.rec_held = 0;
        break;
    case B_REC:
        ui.rec_t = plat_ms();
        rec_toggle();
        break;
    case B_SAVE:
        ui.save_down = 1;
        ui.copy_to = -1;
        break;
    case B_OCTDN: case B_OCTUP: {
        char t[8];
        int o = set.oct + (i == B_OCTUP ? 1 : -1);
        if (o < -3 || o > 3)
            break;
        set.oct = (int8_t)o;
        t[0] = 0;
        if (o > 0)
            text_cat(t, "+");
        int_text(o, t + (o > 0));
        app_say("OCTAVE", t);
        dirty();
        break;
    }
    }
    ui.redraw = 1;
}

static void held_buttons(uint32_t btn)
{
    uint32_t now = plat_ms();
    if (ui.save_down && !(btn >> B_SAVE & 1u)) {   /* SAVE let go: the preset kept (where PRESET pointed) */
        int to = ui.copy_to >= 0 ? ui.copy_to : set.cur;
        ui.save_down = 0;
        if (ui.rec && !eng_playing())
            return;
        set.pre[to] = ed;
        set.cur = (uint8_t)to;
        edited = 0;
        settings_save();
        app_say("SAVED", ed.name);
    }
    if (ui.home_t) {                     /* HOME held: the preset as it was saved */
        if (!(btn >> B_HOME & 1u))
            ui.home_t = 0;
        else if (now - ui.home_t >= 1000u) {
            ui.home_t = 0;
            preset_load(set.cur);
            app_say("REVERT", ed.name);
        }
    }
    if (ui.rec_t) {                      /* REC held: the sequence cleared */
        if (!(btn >> B_REC & 1u))
            ui.rec_t = 0;
        else if (now - ui.rec_t >= 1000u) {
            ui.rec_t = 0;
            memset(&sq.s, 0, sizeof sq.s);
            seq_changed();
            ui.rec = 0;
            ui.cursor = 0;
            app_say("SEQUENCE", "CLEARED");
        }
    }
    if (ui.arp_t) {                      /* ARP held: HOLD */
        if (!(btn >> B_ARP & 1u))
            ui.arp_t = 0;
        else if (now - ui.arp_t >= 1000u) {
            ui.arp_t = 0;
            set.arp_hold = (uint8_t)!set.arp_hold;
            if (!eng_arp_on())
                eng_arp(1, set.arp_mode, set.arp_rate, set.arp_oct, (float)set.arp_gate * 0.01f);
            eng_arp_hold(set.arp_hold);
            page_set(PG_ARP);
            dirty();
            app_say("ARP HOLD", set.arp_hold ? "ON" : "OFF");
        }
    }
}

static void macro_turn(int which, int32_t e)   /* SELECT: VINTAGE, ALGORITHM: VOICE SPREAD */
{
    int p = which ? P_SPREAD : P_VINTAGE, v = ed.p[p] + (int)accel(which ? EN_ALGO : EN_SELECT, e);
    char t[8];
    if (v < 0)
        v = 0;
    if (v > 127)
        v = 127;
    if (v == ed.p[p])
        return;
    knob_put(p, v);
    ui.turned_at = plat_ms();
    ui.turned_what = (uint32_t)which;
    if (ann_on()) {
        int_text(v, t);
        ann_say(PARAMS[p].name, t, ANN_NOW);
    }
    ui.redraw = 1;
}

static void input(void)
{
    uint32_t keys = plat_keys(), btn = plat_buttons(), i;
    int32_t e;
    keys_input(keys);
    for (i = 0; i < NB; i++)
        if ((btn >> i & 1u) && !(ui.btn >> i & 1u))
            button(i);
    held_buttons(btn);
    for (i = 0; i < 4u; i++)
        if ((e = plat_enc(EN_K1 + (int)i)) != 0)
            knob_turn((int)i, e);
    if ((e = plat_enc(EN_SELECT)) != 0)
        macro_turn(0, e);
    if ((e = plat_enc(EN_ALGO)) != 0)
        macro_turn(1, e);
    if ((e = plat_enc(EN_PRESET)) != 0) {
        if (ui.rec && !eng_playing())    /* step record: PRESET walks the steps */
            cursor_move(e > 0 ? 1 : -1);
        else
            preset_browse(e);
    }
    while (mrec_r != mrec_w) {           /* MIDI notes for the recording */
        uint32_t k = mrec_r % 32u;
        if (mrec_on[k])
            rec_down(mrec_note[k]);
        else
            rec_up();
        mrec_r++;
    }
    if (keys != ui.keys || btn != ui.btn)
        ui.redraw = 1;
    ui.keys = keys;
    ui.btn = btn;
}

static void lights(void)
{
    uint32_t lit = 0, dim = 0, btn = 0, dimb, i;
    int v;
    for (v = 0; v < NVOICE; v++) {       /* the keys of the notes sounding */
        int n = eng_voice_note(v), k = n - KEY_BASE - 12 * set.oct;
        if (n >= 0 && k >= 0 && k < NKEYS && eng_voice_env(v) > 0.05f)
            lit |= 1u << k;
    }
    if (ui.rec && !eng_playing())        /* step record: the white keys show where the step is in its bar */
        dim |= 1u << WK[ui.cursor & 15];
    btn |= 1u << PAGES[ui.page].button;
    if (eng_playing())
        btn |= LED_PLAY_GREEN;
    if (ui.rec)
        btn |= 1u << B_REC;
    if (eng_arp_on())
        btn |= 1u << B_ARP;
    if (edited)
        btn |= 1u << B_SAVE;
    if (set.oct < 0)
        btn |= 1u << B_OCTDN;
    if (set.oct > 0)
        btn |= 1u << B_OCTUP;
    dimb = (1u << B_HOME) | (1u << B_SEL) | (1u << B_ENV) | (1u << B_LFO) | (1u << B_EDIT) | (1u << B_FX) | (1u << B_GLO)
           | (1u << B_SEQ) | (1u << B_PLAY) | (1u << B_REC) | (1u << B_ARP);
    for (i = 0; i < NB; i++)
        if (ui.btn >> i & 1u)
            btn |= 1u << i;
    plat_lights(btn & ~(ui.btn & (1u << B_PLAY)), lit, dimb, dim, 1);
}

void app_input_only(void)
{
    if (ui.intro < 0)
        input();
}

/* ------------------------------------------------------------------ MIDI in --- */
/* At 1 kHz (the timer interrupt): notes (the channel set on CLOCK, or all), pitch bend, the mod wheel, sustain,
 * cutoff, and the clock with START, CONTINUE, STOP */
static volatile uint8_t midi_seen;
void app_tick(void)
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = pkt >> 8 & 0xFFu, d1 = pkt >> 16 & 0x7Fu, d2 = pkt >> 24 & 0x7Fu, type = st & 0xF0u;
        if (st == 0xF8u) {
            eng_clock(CLK_TICK);
            continue;
        }
        if (st == 0xFAu) {
            eng_clock(CLK_START);
            continue;
        }
        if (st == 0xFBu) {
            eng_clock(CLK_CONT);
            continue;
        }
        if (st == 0xFCu) {
            eng_clock(CLK_STOP);
            continue;
        }
        if (st >= 0xF0u || (set.mch && (st & 0x0Fu) != set.mch - 1u))
            continue;
        midi_seen = 1;
        if (type == 0x90u && d2) {
            note_down((int)d1, (float)d2 * (1.0f / 127.0f), 0);
            if (mrec_w - mrec_r < 32u) {
                mrec_note[mrec_w % 32u] = (uint8_t)d1;
                mrec_on[mrec_w % 32u] = 1;
                mrec_w++;
            }
        } else if (type == 0x80u || type == 0x90u) {
            note_up((int)d1, 0);
            if (mrec_w - mrec_r < 32u) {
                mrec_note[mrec_w % 32u] = (uint8_t)d1;
                mrec_on[mrec_w % 32u] = 0;
                mrec_w++;
            }
        } else if (type == 0xE0u) {
            eng_bend((float)((int)(d1 | d2 << 7) - 8192) * (1.0f / 8192.0f));
        } else if (type == 0xB0u) {
            if (d1 == 1u)
                eng_mod((float)d2 * (1.0f / 127.0f));
            else if (d1 == 64u)
                eng_sustain(d2 >= 64u);
            else if (d1 == 74u)
                eng_cutoff_cc((int)d2);
            else if (d1 == 123u || d1 == 120u)
                eng_all_off();
        }
    }
}

/* ------------------------------------------------------------------ drawing --- */
/* The look: a Prophet-6's front panel. Black, with walnut cheeks either side; cream legends; red section heads on
 * their tabs; knobs with a red pointer and a ring of ticks; the voices' red LEDs; everything in its section's box. */
#define K_BG RGB(9, 9, 10)
#define K_PANEL RGB(19, 19, 21)
#define K_EDGE RGB(64, 64, 70)
#define K_EDGE_HI RGB(150, 146, 140)
#define K_RED RGB(236, 44, 34)
#define K_RED_D RGB(70, 14, 12)
#define K_RED_M RGB(140, 26, 20)
#define K_CREAM RGB(242, 234, 216)
#define K_GREY RGB(150, 148, 144)
#define K_DIM RGB(58, 58, 62)
#define K_CELL RGB(48, 48, 52)
#define K_KNOB RGB(34, 34, 37)
#define K_KNOB_HI RGB(70, 70, 76)
#define K_GREEN RGB(70, 220, 110)

static int32_t by;                       /* the band being drawn: its first row on the screen */
static uint32_t frame_n;

static uint16_t mix16(uint16_t a, uint16_t b, int32_t t)   /* a .. b, t 0..256 */
{
    int32_t ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31, br = b >> 11, bg = (b >> 5) & 63, bb = b & 31;
    if (t < 0)
        t = 0;
    if (t > 256)
        t = 256;
    return (uint16_t)(((ar + (br - ar) * t / 256) << 11) | ((ag + (bg - ag) * t / 256) << 5) | (ab + (bb - ab) * t / 256));
}

static void ctext(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(cx - text_w(f, s) / 2, y, f, s, c);
}

static void frame_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    cv_rect(x, y, w, 1, c);
    cv_rect(x, y + h - 1, w, 1, c);
    cv_rect(x, y, 1, h, c);
    cv_rect(x + w - 1, y, 1, h, c);
}

/* walnut: the Prophet's wooden cheeks, a grain that never repeats on the screen */
static void wood(int32_t x, int32_t w)
{
    int32_t y, i;
    for (y = 0; y < 240; y++)
        for (i = 0; i < w; i++) {
            uint32_t h = (uint32_t)(y * 7 + i * 13) * 2654435761u;
            int32_t g = (int32_t)((fm_sin_turns((float)y * 0.031f + (float)i * 0.4f) + 1.0f) * 50.0f) + (int32_t)(h >> 28);
            cv_pset(x + i, y, mix16(RGB(58, 30, 16), RGB(124, 72, 38), g + 40));
        }
}

/* a section's box: its edge, its head in red on a dark-red tab, a rule under the head */
static void box(int32_t x, int32_t y, int32_t w, int32_t h, const char *title, const felucca_font_t *f, int lit)
{
    int32_t tw = text_w(f, title) + 10, th = f->h - 2, i;
    cv_rect(x + 1, y + 1, w - 2, h - 2, K_PANEL);
    frame_rect(x, y, w, h, lit ? K_EDGE_HI : K_EDGE);
    for (i = 0; i < th; i++)             /* the tab, its right side cut on a slant */
        cv_rect(x + 2, y + 2 + i, tw + (th - i) / 2, 1, lit ? K_RED_M : K_RED_D);
    cv_rect(x + 2, y + 2 + th, w - 4, 1, lit ? K_RED : K_RED_M);
    cv_text(x + 6, y + 2 + (th - (int32_t)f->h) / 2 + 1, f, title, lit ? K_CREAM : K_RED);
}

static void disc(int32_t cx, int32_t cy, int32_t r, uint16_t c)
{
    int32_t x, y;
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++)
            if (x * x + y * y <= r * r + r)
                cv_pset(cx + x, cy + y, c);
}

/* a knob: ticks around, a dark body with a rim lit from above, a red pointer; v of max (bipolar: from the top) */
static void knob(int32_t cx, int32_t cy, int32_t r, int v, int mx, int lit)
{
    int i;
    float a;
    for (i = 0; i <= 10; i++) {          /* the scale */
        float t = -2.356f + 4.712f * (float)i / 10.0f, s = fm_sinf(t), c = fm_cosf(t);
        int32_t x0 = cx + (int32_t)(s * (float)(r + 3)), y0 = cy - (int32_t)(c * (float)(r + 3));
        int32_t x1 = cx + (int32_t)(s * (float)(r + 6)), y1 = cy - (int32_t)(c * (float)(r + 6));
        cv_line(x0, y0, x1, y1, i == 5 ? K_GREY : K_DIM);
    }
    disc(cx, cy, r, K_KNOB_HI);
    disc(cx, cy + 1, r - 1, K_KNOB);
    disc(cx, cy, r - 3, mix16(K_KNOB, K_KNOB_HI, 90));
    a = -2.356f + 4.712f * (float)v / (float)(mx > 0 ? mx : 1);
    {
        float s = fm_sinf(a), c = fm_cosf(a);
        int32_t x1 = cx + (int32_t)(s * (float)(r - 1)), y1 = cy - (int32_t)(c * (float)(r - 1));
        cv_line(cx, cy, x1, y1, lit ? K_CREAM : K_RED);
        cv_line(cx + 1, cy, x1 + 1, y1, lit ? K_CREAM : K_RED);
    }
    disc(cx, cy, 1, K_DIM);
}

/* the waveform an oscillator makes: shape and width as the engine reads them */
static float wave_ui(float shape, float pw, float ph)
{
    float wt, ws, wp, q;
    pw = 0.5f + 0.45f * pw;
    if (shape < 0.5f) {
        ws = shape * 2.0f;
        wt = 1.0f - ws;
        wp = 0.0f;
    } else {
        wp = (shape - 0.5f) * 2.0f;
        ws = 1.0f - wp;
        wt = 0.0f;
    }
    q = ph < pw ? 1.0f : -1.0f;
    return wt * (1.0f - 4.0f * fm_fabsf(ph - 0.5f)) + ws * (2.0f * ph - 1.0f) + wp * q;
}

static void wave_draw(int32_t x, int32_t y, int32_t w, int32_t h, int shape, int pw, int sync, uint16_t c)
{
    int32_t i, py = 0;
    float s = u7f(shape), p = u7f(pw);
    for (i = 0; i <= w; i++) {
        float ph = (float)i / (float)w * 2.0f, v;   /* two cycles */
        if (sync)                        /* synced: cut short and started again (a fifth's worth) */
            ph = ph * 1.5f;
        ph -= (float)(int)ph;
        v = wave_ui(s, p, ph);
        {
            int32_t yy = y + h / 2 - (int32_t)(v * (float)(h / 2 - 1));
            if (i > 0)
                cv_line(x + i - 1, py, x + i, yy, c);
            py = yy;
        }
    }
}

/* the ADSR's shape (times on a log scale) */
static void adsr_draw(int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t *p, uint16_t c)
{
    int32_t wa = 4 + p[0] * 22 / 127, wd = 4 + p[1] * 22 / 127, wr = 4 + p[3] * 22 / 127;
    int32_t ws = w - wa - wd - wr, ys = y + h - 1 - p[2] * (h - 2) / 127, x0 = x, xa = x0 + wa, xd = xa + wd,
            xs = xd + (ws > 4 ? ws : 4), xr = xs + wr, yb = y + h - 1;
    if (xr > x + w)
        xr = x + w;
    cv_line(x0, yb, xa, y, c);
    cv_line(xa, y, xd, ys, c);
    cv_line(xd, ys, xs, ys, c);
    cv_line(xs, ys, xr, yb, c);
    cv_rect(xa - 1, y - 1, 3, 3, c);
    cv_rect(xd - 1, ys - 1, 3, 3, c);
    cv_rect(xs - 1, ys - 1, 3, 3, c);
    cv_rect(xr - 1, yb - 1, 3, 3, c);
    cv_rect(x0, yb, w, 1, K_DIM);
}

/* ---- the head: the name, the preset ---- */
static void draw_head(void)
{
    char t[4];
    const felucca_font_t *nf;
    int32_t bx = 135, bw = 101, i, ax = bx + bw - 10;
    cv_text(7, 1, &FONT_M, "PROPHET 67", K_CREAM);
    cv_rect(131, 4, 1, 18, K_GREY);
    frame_rect(bx, 2, bw, 22, K_CREAM);
    frame_rect(bx + 1, 3, bw - 2, 20, K_DIM);
    t[0] = (char)('0' + (set.cur + 1) / 10);   /* its slot, small and red, then its name */
    t[1] = (char)('0' + (set.cur + 1) % 10);
    t[2] = 0;
    cv_text(bx + 4, 8, &FONT_XS, t, K_RED);
    nf = text_w(&FONT_B, ed.name) <= bw - 32 ? &FONT_B : &FONT_XS;
    cv_text(bx + 18, nf == &FONT_B ? 6 : 8, nf, ed.name, K_CREAM);
    for (i = 0; i < 6; i++)              /* the arrow: PRESET turns them */
        cv_rect(ax + i, 7 + i, 1, 12 - 2 * i, K_CREAM);
    if (edited)                          /* edited, not saved yet */
        cv_rect(bx + bw - 5, 4, 2, 2, K_RED);
    cv_rect(4, 26, 232, 2, K_RED);
}

/* ---- the voices' LEDs ---- */
static void draw_voices(void)
{
    int v, n = eng_voices();
    char t[8];
    cv_text(8, 30, &FONT_XS, "VOICES:", K_CREAM);
    for (v = 0; v < NVOICE; v++) {
        float e = eng_voice_env(v);
        int32_t x = 58 + v * 20, y = 31, g = (int32_t)(e * 256.0f);
        if (g > 40) {                    /* the glow around a lit one */
            uint16_t h = mix16(K_BG, K_RED_D, g);
            cv_rect(x - 2, y - 2, 14, 14, h);
        }
        cv_rect(x, y, 10, 10, mix16(RGB(48, 10, 8), K_RED, g + (e > 0.0f ? 30 : 0)));
        if (g > 120)
            cv_rect(x + 2, y + 2, 3, 3, mix16(K_RED, RGB(255, 190, 170), g - 120));
    }
    cv_rect(199, 30, 1, 13, K_GREY);
    int_text(n, t);
    text_cat(t, "/7");
    cv_text(205, 29, &FONT_B, t, K_CREAM);
}

/* ---- HOME: the oscillators, the filter, the envelope (the panel at a glance) ---- */
static void osc_box(int32_t x, int32_t w, const char *title, int pfreq, int pshape, int ppw, int pfine, int lit)
{
    char t[10];
    int sync = pfine && ed.p[P_O2_MODE] == O2M_SYNC ? 0 : (!pfine && ed.p[P_O2_MODE] == O2M_SYNC);
    box(x, 45, w, 44, title, &FONT_B, lit);
    wave_draw(x + 5, 64, 44, 20, ed.p[pshape], ed.p[ppw], sync, K_CREAM);
    cv_text(x + 55, 60, &FONT_XS, "FREQ", K_GREY);
    param_text(pfreq, ed.p[pfreq], t);
    cv_text(x + 55, 71, &FONT_B, t, K_CREAM);
    cv_rect(x + 83, 62, 1, 24, K_DIM);
    cv_text(x + 87, 60, &FONT_XS, pfine ? "FINE" : "SUB", K_GREY);
    if (pfine)
        param_text(pfine, ed.p[pfine], t);
    else
        int_text(ed.p[P_O1_SUB], t);
    cv_text(x + 87, 73, &FONT_XS, t, K_CREAM);
}

static void draw_home(void)
{
    static const char *const KN[4] = {"CUTOFF", "RESO", "ENV", "HPF"};
    static const int KP[4] = {P_CUTOFF, P_RESO, P_ENV_AMT, P_HPF};
    char t[10];
    int i;
    int fenv = ui.page != PG_AENV;
    osc_box(4, 115, "OSC 1", P_O1_FREQ, P_O1_SHAPE, P_O1_PW, 0, ui.page == PG_OSC1);
    osc_box(121, 115, ed.p[P_O2_MODE] == O2M_SYNC ? "OSC 2 SYNC" : ed.p[P_O2_MODE] >= O2M_LOW ? "OSC 2 LOW" : "OSC 2",
            P_O2_FREQ, P_O2_SHAPE, P_O2_PW, P_O2_FINE, ui.page == PG_OSC2);
    box(4, 91, 156, 57, "FILTER", &FONT_B, 1);
    for (i = 0; i < 4; i++) {
        int32_t cx = 23 + i * 39;
        ctext(cx, 107, &FONT_XS, KN[i], K_CREAM);
        knob(cx, 125, 8, ed.p[KP[i]], 127, 0);
        param_text(KP[i], ed.p[KP[i]], t);
        ctext(cx, 136, &FONT_XS, t, K_CREAM);
    }
    box(163, 91, 73, 57, fenv ? "F ENV" : "A ENV", &FONT_B, 0);
    adsr_draw(168, 109, 63, 19, &ed.p[fenv ? P_FA : P_AA], K_CREAM);
    for (i = 0; i < 4; i++) {
        static const char *const L[4] = {"A", "D", "S", "R"};
        int32_t cx = 172 + i * 18;
        ctext(cx, 130, &FONT_XS, L[i], K_GREY);
        int_text(ed.p[(fenv ? P_FA : P_AA) + i], t);
        ctext(cx, 138, &FONT_XS, t, K_CREAM);
    }
}

/* ---- any other page: its visual, then its four knobs ---- */
static void trace_draw(const float *tr, int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    int32_t i, py = 0;
    for (i = 0; i < 64 && i < w; i++) {
        float v = tr[(ui.trace_w + (uint32_t)i) & 63u];
        int32_t yy = y + h / 2 - (int32_t)(v * (float)(h / 2 - 1));
        int32_t xx = x + i * w / 64;
        if (i)
            cv_line(xx - w / 64, py, xx, yy, c);
        py = yy;
    }
}

static void page_visual(int32_t x, int32_t y, int32_t w, int32_t h)
{
    char t[16];
    int i;
    switch (ui.page) {
    case PG_OSC1:
        wave_draw(x + 4, y + 2, w - 8, h - 4, ed.p[P_O1_SHAPE], ed.p[P_O1_PW], ed.p[P_O2_MODE] == O2M_SYNC, K_CREAM);
        break;
    case PG_OSC2:
        wave_draw(x + 4, y + 2, w - 8, h - 4, ed.p[P_O2_SHAPE], ed.p[P_O2_PW], 0, K_CREAM);
        break;
    case PG_MIXER: {
        static const char *const L[4] = {"OSC 1", "OSC 2", "SUB", "NOISE"};
        const int pv[4] = {ed.p[P_MIX_O1], ed.p[P_MIX_O2], ed.p[P_O1_SUB], ed.p[P_NOISE]};
        for (i = 0; i < 4; i++) {
            int32_t bx = x + 8 + i * 56, bw = 40, fill = pv[i] * (bw - 2) / 127;
            cv_text(bx, y + 2, &FONT_XS, L[i], K_GREY);
            cv_rect(bx, y + 16, bw, 8, K_CELL);
            cv_rect(bx + 1, y + 17, fill, 6, K_RED);
        }
        break;
    }
    case PG_FENV:
        adsr_draw(x + 8, y + 3, w - 16, h - 6, &ed.p[P_FA], K_CREAM);
        break;
    case PG_AENV:
        adsr_draw(x + 8, y + 3, w - 16, h - 6, &ed.p[P_AA], K_CREAM);
        break;
    case PG_LFO2:
        trace_draw(ui.lfo2_trace, x + 8, y + 2, w - 16, h - 4, K_CREAM);
        if (ed.p[P_L2_SYNC])
            cv_text(x + w - 40, y - 14, &FONT_XS, "SYNC", K_RED);
        break;
    case PG_LSYNC:
        trace_draw(ui.lfo_trace, x + 8, y + 2, w - 16, h - 4, K_CREAM);
        trace_draw(ui.lfo2_trace, x + 8, y + 2, w - 16, h - 4, K_RED);
        cv_text(x + 8, y + 1, &FONT_XS, "1", K_CREAM);
        cv_text(x + 18, y + 1, &FONT_XS, "2", K_RED);
        break;
    case PG_LFO:
        if (ed.p[P_L1_SYNC])
            cv_text(x + w - 40, y - 14, &FONT_XS, "SYNC", K_RED);
        /* fall through */
    case PG_FXA:
        trace_draw(ui.lfo_trace, x + 8, y + 2, w - 16, h - 4, ui.page == PG_LFO ? K_CREAM : K_RED);
        if (ui.page == PG_FXA) {         /* the chorus: two sweeps a quarter apart */
            int32_t k, py = 0, py2 = 0;
            float ph = (float)frame_n * 0.002f * (0.5f + u7f(ed.p[P_CH_RATE]) * 4.0f);
            for (k = 0; k < w - 16; k++) {
                float a = ph + (float)k / (float)(w - 16);
                int32_t yy = y + h / 2 - (int32_t)(fm_sin_turns(a) * u7f(ed.p[P_CH_DEPTH]) * (float)(h / 2 - 2));
                int32_t y2 = y + h / 2 - (int32_t)(fm_sin_turns(a + 0.25f) * u7f(ed.p[P_CH_DEPTH]) * (float)(h / 2 - 2));
                if (k) {
                    cv_line(x + 8 + k - 1, py, x + 8 + k, yy, K_CREAM);
                    cv_line(x + 8 + k - 1, py2, x + 8 + k, y2, K_GREY);
                }
                py = yy;
                py2 = y2;
            }
        }
        break;
    case PG_POLY: {                      /* the routing: two sources into five destinations */
        static const char *const D[5] = {"FREQ", "SHAPE", "PW", "LPF", "HPF"};
        int on[5];
        on[0] = ed.p[P_PM_DOSC] & PMO_FREQ;
        on[1] = ed.p[P_PM_DOSC] & PMO_SHAPE;
        on[2] = ed.p[P_PM_DOSC] & PMO_PW;
        on[3] = ed.p[P_PM_DFILT] == PMF_LPF || ed.p[P_PM_DFILT] == PMF_BOTH;
        on[4] = ed.p[P_PM_DFILT] == PMF_HPF || ed.p[P_PM_DFILT] == PMF_BOTH;
        cv_text(x + 6, y + 3, &FONT_XS, "FILT ENV", ed.p[P_PM_FENV] != 64 ? K_CREAM : K_GREY);
        cv_text(x + 6, y + 17, &FONT_XS, "OSC 2", ed.p[P_PM_OSC2] != 64 ? K_CREAM : K_GREY);
        for (i = 0; i < 5; i++) {
            int32_t dx = x + 78 + i * 30;
            cv_rect(dx, y + 9, 27, 13, on[i] ? K_RED : K_CELL);
            ctext(dx + 14, y + 10, &FONT_XS, D[i], on[i] ? K_CREAM : K_GREY);
            if (on[i]) {
                if (ed.p[P_PM_FENV] != 64)
                    cv_line(x + 56, y + 9, dx + 13, y + 9, K_RED_M);
                if (ed.p[P_PM_OSC2] != 64)
                    cv_line(x + 40, y + 23, dx + 13, y + 22, K_RED_M);
            }
        }
        break;
    }
    case PG_FXB:
    case PG_SPACE: {                     /* the tail: as it is now, over time */
        trace_draw(ui.tail_trace, x + 8, y + 2, w - 16, h - 4, K_RED);
        cv_text(x + w - 60, y + 2, &FONT_XS, PARAMS[P_RV_TYPE].choice[ed.p[P_RV_TYPE]], K_CREAM);
        if (ui.page == PG_SPACE && ed.p[P_RV_TYPE] != RV_SPACE)
            cv_text(x + 8, y + 2, &FONT_XS, "REVERB TYPE: SPACE", K_GREY);
        break;
    }
    case PG_VOICE: {                     /* where each voice stands (SPREAD) */
        float s = u7f(ed.p[P_SPREAD]);
        static const float PAN[NVOICE] = {-0.95f, 0.9f, -0.35f, 0.45f, -0.7f, 0.75f, 0.05f};
        cv_rect(x + 10, y + h / 2, w - 20, 1, K_DIM);
        cv_text(x + 10, y + 2, &FONT_XS, "L", K_GREY);
        cv_text(x + w - 16, y + 2, &FONT_XS, "R", K_GREY);
        for (i = 0; i < NVOICE; i++) {
            int32_t px = x + w / 2 + (int32_t)(PAN[i] * s * (0.6f + 0.4f * s) * (float)(w / 2 - 14));
            int32_t g = (int32_t)(eng_voice_env(i) * 256.0f);
            cv_rect(px - 3, y + h / 2 - 3, 7, 7, mix16(RGB(60, 12, 10), K_RED, g + 40));
        }
        break;
    }
    case PG_CLOCK:
        int_text((int32_t)(eng_bpm() + 0.5f), t);
        cv_text(x + 10, y + 1, &FONT_L, t, K_CREAM);
        cv_text(x + 12 + text_w(&FONT_L, t), y + 14, &FONT_B, eng_clock_ext() ? "BPM EXT" : "BPM", K_RED);
        t[0] = 0;                        /* the load (for testing on the FM-1) */
        text_cat(t, "CPU ");
        int_text((int32_t)plat_cpu_pct(), t + 4);
        text_cat(t, "%");
        cv_text(x + w - 70, y + 14, &FONT_B, t, K_GREY);
        break;
    case PG_ARP: {
        int n = eng_arp_note();
        note_text(n, t);
        cv_text(x + 10, y + 1, &FONT_L, n >= 0 ? t : "--", K_CREAM);
        cv_text(x + 110, y + 4, &FONT_B, eng_arp_on() ? "ON" : "OFF", eng_arp_on() ? K_RED : K_GREY);
        if (set.arp_hold)
            cv_text(x + 150, y + 4, &FONT_B, "HOLD", K_CREAM);
        break;
    }
    case PG_SEQ: {                       /* the bars: those with notes, the one playing */
        int b, st = eng_seq_step();
        for (b = 0; b < SEQ_BARS; b++) {
            int32_t bx = x + 10 + b * 13;
            int has = 0, k;
            for (k = 0; k < 16; k++)
                if (sq.s.note[b * 16 + k][0])
                    has = 1;
            cv_rect(bx, y + 8, 11, 14, b < set.seq_bars ? (has ? K_RED_M : K_CELL) : K_PANEL);
            if (b >= set.seq_bars)
                frame_rect(bx, y + 8, 11, 14, K_CELL);
            if ((st >= 0 && st / 16 == b) || (ui.rec && !eng_playing() && ui.cursor / 16 == b))
                frame_rect(bx - 1, y + 7, 13, 16, K_CREAM);
        }
        break;
    }
    default: {                           /* FILTER 2: the two filters' response */
        int32_t k, py = 0;
        float lc = u7f(ed.p[P_CUTOFF]), hc = u7f(ed.p[P_HPF]) * 0.93f, rs = u7f(ed.p[P_RESO]);
        float hr = u7f(ed.p[P_HP_RESO]);
        for (k = 0; k < w - 16; k++) {
            float f = (float)k / (float)(w - 17), dl = (f - lc) * 10.0f, dh = (hc - f) * 10.0f, g;
            g = -fm_maxf(0.0f, dl) * 24.0f - fm_maxf(0.0f, dh) * 12.0f;
            g += rs * 14.0f * fm_expf(-dl * dl * 8.0f) + hr * 10.0f * fm_expf(-dh * dh * 8.0f);
            {
                int32_t yy = y + 8 - (int32_t)(g * (float)(h - 10) / 48.0f);
                if (yy < y)
                    yy = y;
                if (yy > y + h - 1)
                    yy = y + h - 1;
                if (k)
                    cv_line(x + 8 + k - 1, py, x + 8 + k, yy, K_CREAM);
                py = yy;
            }
        }
    }
    }
}

static void draw_page(void)
{
    const page_t *pg = &PAGES[ui.page];
    int i;
    box(4, 45, 232, 103, pg->title, &FONT_B, 1);
    page_visual(6, 62, 228, 30);
    for (i = 0; i < 4; i++) {
        int code = pg->k[i], v = knob_get(code);
        int32_t cx = 33 + i * 58;
        char t[16];
        ctext(cx, 95, &FONT_XS, knob_name(code), K_CREAM);
        if (code < NPARAM && PARAMS[code].kind == PK_CHOICE) {   /* a choice: a window with its name */
            int j, n = PARAMS[code].max + 1;
            frame_rect(cx - 26, 110, 52, 18, K_GREY);
            param_text(code, v, t);
            ctext(cx, 113, text_w(&FONT_B, t) <= 50 ? &FONT_B : &FONT_XS, t, K_RED);
            for (j = 0; j < n && n <= 8; j++)
                cv_rect(cx - n * 3 + j * 6, 131, 4, 3, j == v ? K_RED : K_DIM);
        } else {
            int mn = knob_min(code), mx = knob_max(code);
            if (rate_sync(code) >= 0) {
                v = ed.p[rate_sync(code)] - 1;
                mn = 0;
                mx = NLSY - 2;
            }
            knob(cx, 119, 11, v - mn, mx - mn, rate_sync(code) >= 0);
            knob_text(code, t);
            ctext(cx, 134, &FONT_XS, t, K_CREAM);
        }
        if (i)
            cv_rect(4 + i * 58, 96, 1, 48, K_DIM);
    }
}

/* ---- POLY MOD, VOICE SPREAD, VINTAGE ---- */
static void slider(int32_t x, int32_t y, int32_t w, int v)
{
    int32_t f = v * (w - 6) / 127;
    cv_rect(x, y, w, 6, K_CELL);
    cv_rect(x, y, f + 3, 6, K_RED);
    cv_rect(x + f, y - 3, 6, 12, K_CREAM);
    cv_rect(x + f + 2, y - 2, 2, 10, K_GREY);
}

static void draw_row3(void)
{
    char t[12];
    uint32_t now = plat_ms();
    int vl = ui.turned_what == 0 && now - ui.turned_at < 900u, sl = ui.turned_what == 1 && now - ui.turned_at < 900u;
    /* POLY MOD: the two sources' amounts, where they go */
    box(4, 150, 80, 42, "POLY MOD", &FONT_B, ui.page == PG_POLY);
    cv_text(8, 167, &FONT_XS, "FE", K_GREY);
    param_text(P_PM_FENV, ed.p[P_PM_FENV], t);
    cv_text(22, 167, &FONT_XS, t, K_CREAM);
    cv_text(45, 167, &FONT_XS, "O2", K_GREY);
    param_text(P_PM_OSC2, ed.p[P_PM_OSC2], t);
    cv_text(59, 167, &FONT_XS, t, K_CREAM);
    {
        char d[20] = "";
        param_text(P_PM_DOSC, ed.p[P_PM_DOSC], d);
        if (ed.p[P_PM_DFILT]) {
            char f[10];
            param_text(P_PM_DFILT, ed.p[P_PM_DFILT], f);
            text_cat(d, ed.p[P_PM_DOSC] ? "+" : "");
            if (ed.p[P_PM_DOSC])
                text_cat(d, f[0] == 'L' && f[3] == '+' ? "FLT" : f);
            else {
                d[0] = 0;
                text_cat(d, f);
            }
        }
        cv_text(8, 179, &FONT_XS, d, ed.p[P_PM_DOSC] || ed.p[P_PM_DFILT] ? K_RED : K_GREY);
    }
    /* VOICE SPREAD (ALGORITHM) */
    box(87, 150, 76, 42, "SPREAD", &FONT_B, sl);
    slider(92, 170, 66, ed.p[P_SPREAD]);
    if (sl) {
        int_text(ed.p[P_SPREAD], t);
        ctext(125, 180, &FONT_XS, t, K_CREAM);
    } else {
        cv_text(91, 180, &FONT_XS, "CENTER", K_GREY);
        cv_text(137, 180, &FONT_XS, "WIDE", K_GREY);
    }
    /* VINTAGE (SELECT) */
    box(166, 150, 70, 42, "VINTAGE", &FONT_B, vl);
    slider(171, 170, 60, ed.p[P_VINTAGE]);
    if (vl) {
        int_text(ed.p[P_VINTAGE], t);
        ctext(201, 180, &FONT_XS, t, K_CREAM);
    } else {
        cv_text(170, 180, &FONT_XS, "CLEAN", K_GREY);
        cv_text(208, 180, &FONT_XS, "AGED", K_GREY);
    }
}

/* ---- the sequencer's strip: the bar playing (or being written), its sixteen steps ---- */
static void draw_seq(void)
{
    int st = eng_seq_step(), bar, i;
    int writing = ui.rec && !eng_playing();
    char t[16];
    cv_rect(4, 194, 232, 1, K_EDGE);
    cv_text(8, 196, &FONT_B, writing ? "STEP REC" : ui.rec ? "REC" : "SEQUENCER", ui.rec ? K_RED : K_RED);
    bar = writing ? ui.cursor / 16 : st >= 0 ? st / 16 : 0;
    t[0] = 0;
    text_cat(t, "BAR ");
    int_text(bar + 1, t + 4);
    text_cat(t, "/");
    int_text(set.seq_bars, t + text_len_(t));
    cv_text(writing ? 80 : 90, 199, &FONT_XS, t, K_CREAM);
    {                                    /* the transport: play, stop, or the arp's badge */
        int32_t px = 146, i2;
        if (eng_playing())
            for (i2 = 0; i2 < 6; i2++)
                cv_rect(px + i2, 199 + i2 / 2, 1, 11 - i2 / 2 * 2, K_GREEN);
        else
            cv_rect(px, 200, 9, 9, K_GREY);
        if (ui.rec)
            disc(px + 16, 204, 4, K_RED);
    }
    int_text((int32_t)(eng_bpm() + 0.5f), t);
    cv_text(172, 199, &FONT_XS, t, K_CREAM);
    cv_text(194, 199, &FONT_XS, eng_arp_on() ? "ARP" : RATE_NAME[set.seq_rate], eng_arp_on() ? K_RED : K_GREY);
    for (i = 0; i < 16; i++) {
        int s = bar * 16 + i, n = 0, k, tie;
        int32_t x = 8 + i * 14, y = 213;
        const uint8_t *nt = sq.s.note[s];
        tie = nt[0] == SN_TIE;
        for (k = 0; k < SEQ_POLY; k++)
            if (nt[k] >= 12)
                n++;
        cv_rect(x, y, 12, 16, s < seq_len() ? K_CELL : K_PANEL);
        cv_rect(x, y, 12, 3, s < seq_len() ? RGB(66, 66, 70) : K_PANEL);
        if (n) {                         /* its notes: the more, the fuller */
            int32_t hgt = 6 + n * 10 / SEQ_POLY;
            cv_rect(x, y + 16 - hgt, 12, hgt, K_RED);
        }
        if (tie)
            cv_rect(x - 2, y + 9, 16, 4, K_RED_M);
        if (s == st)
            frame_rect(x - 1, y - 1, 14, 18, K_CREAM);
        if (writing && s == ui.cursor && (frame_n & 8u))
            frame_rect(x - 1, y - 1, 14, 18, K_CREAM);
        if ((i & 3) == 0) {
            int_text(i + 1, t);
            cv_text(x + (i < 9 ? 3 : 0), 230, &FONT_XS, t, i == 0 ? K_CREAM : K_GREY);
        }
    }
    if (st >= 0 && st / 16 == bar)       /* the playhead */
        cv_rect(8 + (st & 15) * 14 + 6, 211, 1, 20, K_CREAM);
}

static void draw_msg(void)
{
    int32_t w = 200, x = 20, y = 92;
    if (!ui.msg_until)
        return;
    cv_rect(x, y, w, 44, K_BG);
    frame_rect(x, y, w, 44, K_RED);
    frame_rect(x + 2, y + 2, w - 4, 40, K_RED_D);
    ctext(120, y + 5, &FONT_B, ui.msg[0], K_RED);
    ctext(120, y + 22, &FONT_B, ui.msg[1], K_CREAM);
}

static void draw_scene(void)
{
    cv_oy = -by;
    wood(0, 3);
    wood(237, 3);
    draw_head();
    draw_voices();
    if (ui.page == PG_FILTER)
        draw_home();
    else
        draw_page();
    draw_row3();
    draw_seq();
    draw_msg();
}

static void draw(void)
{
    frame_n++;
    for (by = 0; by < 240; by += 120) {
        cv_begin(240, 120, K_BG);
        draw_scene();
        cv_oy = 0;
        cv_blit(0, (uint32_t)by);
    }
    ui.drawn_at = plat_ms();
}

/* ---------------------------------------------------------- splash, intro --- */
static void draw_logo(int lit)          /* the name, the red rule, the seven voices lighting one by one */
{
    int v;
    for (by = 0; by < 240; by += 120) {
        cv_begin(240, 120, K_BG);
        cv_oy = -by;
        wood(0, 6);
        wood(234, 6);
        ctext(120, 70, &FONT_L, "PROPHET 67", K_CREAM);
        cv_rect(30, 108, 180, 2, K_RED);
        ctext(120, 116, &FONT_XS, "7-VOICE POLYPHONIC SYNTHESIZER", K_GREY);
        for (v = 0; v < NVOICE; v++) {
            int32_t x = 120 - 3 * 20 - 5 + v * 20;
            cv_rect(x, 140, 10, 10, v < lit ? K_RED : RGB(48, 10, 8));
        }
        ctext(120, 196, &FONT_XS, "by swtdr34mz", K_GREY);
        cv_oy = 0;
        cv_blit(0, (uint32_t)by);
    }
}

void app_splash(void)
{
    lcd_fill(0, 0, 240, 240, K_BG);
    draw_logo(0);
}

#define INTRO_STEP 110u
static int intro_frame(uint32_t now)
{
    int k;
    if (ui.intro < 0)
        return 0;
    if (plat_keys() || plat_buttons()) {
        ui.intro = -1;
        ui.keys = plat_keys();
        ui.btn = plat_buttons();
        ui.redraw = 1;
        return 0;
    }
    k = (int)((now - ui.intro_at) / INTRO_STEP);
    if (k > NVOICE + 5) {
        ui.intro = -1;
        ui.redraw = 1;
        return 0;
    }
    if (k > ui.intro && k <= NVOICE) {
        ui.intro = (int8_t)k;
        draw_logo(k);
    }
    return 1;
}

/* ------------------------------------------------------------- the contract --- */
void app_init(void)
{
    int k;
    plat_cpu2_set(1);                    /* five voices on the second core (one core still works) */
    eng_init();
    settings_load();
    preset_load(set.cur);
    eng_seq_bind(&sq.s);
    sound_globals();
    for (k = 0; k < NKEYS; k++)
        key_sounding[k] = -1;
    ui.copy_to = -1;
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    eng_render(lr, n, (float)master_q12 * (1.0f / 4096.0f) * 8300000.0f);
}

void app_overload(void) { eng_overload(); }

void app_panic(void)
{
    eng_play(0);
    eng_panic();
}

void app_ui_init(void)
{
    ui.redraw = 1;
    ui.intro = 0;
    ui.intro_at = plat_ms();
    draw_logo(0);
}

void app_frame(void)
{
    uint32_t now = plat_ms();
    if (intro_frame(now)) {
        plat_lights(0, 0, 0, 0, 0);
        return;
    }
    input();
    lights();
    if (ann_on() && ann_where()) {
        char t[64] = "PROPHET 67. ";
        text_cat(t, ed.name);
        text_cat(t, ". ");
        text_cat(t, PAGES[ui.page].title);
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(now);
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (set_dirty && now - dirty_at > 4000u && !eng_playing())   /* (a flash write hiccups the sound: not mid-song) */
        settings_save();
    if (seq_dirty && now - seq_dirty_at > 2500u && !eng_playing() && !ui.rec_held)
        seq_save();
    if (now - ui.drawn_at >= 33u) {      /* the LEDs, the traces, the playhead: 30 frames a second */
        ui.lfo_trace[ui.trace_w & 63u] = eng_lfo(0);
        ui.lfo2_trace[ui.trace_w & 63u] = eng_lfo(1);
        ui.tail_trace[ui.trace_w & 63u] = fm_minf(1.0f, eng_tail() * 1.6f) * 2.0f - 1.0f;
        ui.trace_w++;
        ui.redraw = 1;
    }
    if (ui.redraw) {
        ui.redraw = 0;
        draw();
    }
}

/* ------------------------------------------------------------- backup ------ */
int app_backup_playing(void) { return eng_playing(); }
void app_backup_flush(void)
{
    if (set_dirty)
        settings_save();
    if (seq_dirty)
        seq_save();
}
void app_backup_hold(int on) { (void)on; }
void app_backup_restored(void)
{
    settings_load();
    preset_load(set.cur);
    sound_globals();
    ui.redraw = 1;
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "page"))
        *got = ui.page;
    else if (!strcmp(what, "cur"))
        *got = set.cur;
    else if (!strcmp(what, "edited"))
        *got = edited;
    else if (!strcmp(what, "voices"))
        *got = eng_voices();
    else if (!strcmp(what, "notes"))
        *got = (int)eng_notes();
    else if (!strcmp(what, "steals"))
        *got = (int)eng_steals();
    else if (!strcmp(what, "playing"))
        *got = eng_playing();
    else if (!strcmp(what, "step"))
        *got = eng_seq_step();
    else if (!strcmp(what, "rec"))
        *got = ui.rec;
    else if (!strcmp(what, "cursor"))
        *got = ui.cursor;
    else if (!strcmp(what, "arp"))
        *got = eng_arp_on();
    else if (!strcmp(what, "arpsteps"))
        *got = (int)eng_arp_steps();
    else if (!strcmp(what, "oct"))
        *got = set.oct;
    else if (!strcmp(what, "bpm"))
        *got = (int)(eng_bpm() + 0.5f);
    else if (!strcmp(what, "ext"))
        *got = eng_clock_ext();
    else if (!strcmp(what, "vintage"))
        *got = ed.p[P_VINTAGE];
    else if (!strcmp(what, "spread"))
        *got = ed.p[P_SPREAD];
    else if (!strcmp(what, "dirty"))
        *got = set_dirty;
    else if (!strcmp(what, "seqdirty"))
        *got = seq_dirty;
    else if (!strncmp(what, "s", 1) && what[1] >= '0' && what[1] <= '9') {   /* sN: step N's first note */
        int s = atoi(what + 1);
        *got = s >= 0 && s < SEQ_STEPS ? sq.s.note[s][0] : -1;
    } else if (!strncmp(what, "n", 1) && what[1] >= '0' && what[1] <= '9') { /* nN: step N's notes */
        int s = atoi(what + 1), k, c = 0;
        for (k = 0; k < SEQ_POLY && s >= 0 && s < SEQ_STEPS; k++)
            if (sq.s.note[s][k] >= 12)
                c++;
        *got = c;
    } else if (what[0] == 'k' && what[1] >= '1' && what[1] <= '4' && !what[2])
        *got = knob_get(PAGES[ui.page].k[what[1] - '1']);
    else if (!strcmp(what, "level"))     /* the output, x1000 */
        *got = (int)(eng_level() * 1000.0f);
    else if (!strcmp(what, "tail"))
        *got = (int)(eng_tail() * 1000.0f);
    else
        return 0;
    return 1;
}
#endif
