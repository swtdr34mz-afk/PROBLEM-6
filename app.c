/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (PROBLEM 6) */
/* PROBLEM 6: the panel (input, pages, knobs), the presets, the sequencer's editing, MIDI in and out, the screen (a
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
 *   SEQ             TRACK page (TRACK 1-2: the one the keys play, the pages edit, REC writes; LENGTH 1-64, RATE,
 *                   TRANSPOSE); again MUTES & CLICK (MUTE 1, MUTE 2, the metronome's CLICK, VOLUME)
 *   PLAY            the four tracks from the top / stop (SEQ held 1 s: the metronome on / off; GLO's 4th page: METRONOME)
 *   REC             record into the selected track: stopped, step by step on the STEP EDIT page (a chord at a time,
 *                   at its velocity; K1 / PRESET move the step, K2 its VELOCITY, K3 its GATE, K4 its NOTE; SEQ a rest,
 *                   SAVE a tie); playing, live into the nearest steps. Held 1 s: the track cleared
 *   SAVE            the edited preset kept in its slot; held + PRESET: into another slot (on letting go)
 *   MIDI in (USB, TRS): notes, pitch bend, CC 1 (mod wheel), 64 (sustain), 74 (cutoff), clock, START, STOP.
 *   MIDI out (USB): the keys' notes. */
#include "fastmath.h"
#include "engine.h"

/* ----------------------------------------------------------------- settings --- */
#define SET_MAGIC 0x35374850u            /* "PH75": the settings record (v0.6: the DELAY) */
#define SET_MAGIC_V4 0x34374850u         /* "PH74": v0.5's: 66 parameters a preset, the LOW SPLIT after the globals */
#define SET_MAGIC_V3 0x33374850u         /* "PH73": v0.3/0.4's: 62 parameters a preset */
#define SET_MAGIC_V2 0x32374850u         /* "PH72": v0.2's: 62, CUTOFF 0..127 */
#define SET_MAGIC_V1 0x31374850u         /* "PH71": v0.1's: 54, CUTOFF 0..127 */
#define SEQ_MAGIC_V1 0x51534850u         /* "PHSQ": up to v1.0, one line of 16 bars of chords */
#define SONG_MAGIC_4 0x34534850u         /* "PHS4": a v1.1 test build's: four tracks */
#define SONG_MAGIC 0x32534850u           /* "PHS2": v1.1, two tracks of 64 steps, each its own sound */
#define OLD_BARS 16
#define FX_PARAM(p) ((p) == P_CH_MIX || (p) == P_CH_RATE || (p) == P_CH_DEPTH || (p) == P_DIST || ((p) >= P_RV_TYPE && (p) <= P_SP_PRE) || ((p) >= P_DL_TIME_L && (p) <= P_DL_SYNC))

/* the LOW SPLIT's scales (OFFWORLD's) */
typedef struct {
    const char *name;
    uint8_t n;
    uint8_t st[7];
} scale_t;
static const scale_t SCALES[] = {
    {"MAJOR", 7, {0, 2, 4, 5, 7, 9, 11}},
    {"MINOR", 7, {0, 2, 3, 5, 7, 8, 10}},
    {"DORIAN", 7, {0, 2, 3, 5, 7, 9, 10}},
    {"PHRYGIAN", 7, {0, 1, 3, 5, 7, 8, 10}},
    {"LYDIAN", 7, {0, 2, 4, 6, 7, 9, 11}},
    {"MIXOLYD", 7, {0, 2, 4, 5, 7, 9, 10}},
    {"HARM MIN", 7, {0, 2, 3, 5, 7, 8, 11}},
    {"PENTA MAJ", 5, {0, 2, 4, 7, 9}},
    {"PENTA MIN", 5, {0, 3, 5, 7, 10}},
    {"BLUES", 6, {0, 3, 5, 6, 7, 10}},
    {"ARABIC", 7, {0, 1, 4, 5, 7, 8, 11}},
};
#define NSCALE (sizeof SCALES / sizeof SCALES[0])
/* where the right hand's scale stands: in KEY; on the ROOT the left hand plays (the lowest note sounding there, as
 * OFFWORLD's FOLLOW); or on that root with the scale laid on the white keys (OFFWORLD's WHITE: no wrong note) */
enum { FOLLOW_KEY, FOLLOW_ROOT, FOLLOW_WHITE, NFOLLOW };
static const char *const FOLLOW_NAME[NFOLLOW] = {"KEY", "ROOT", "WHITE"};
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
    uint8_t click, click_vol, rsv[1];    /* v1.1: the metronome (0 off, 1 on, 2 while recording), its volume (0: 60) */
    /* v0.5: the LOW SPLIT */
    uint8_t split_oct;                   /* 0 off, 1..3 octaves down */
    uint8_t split_key;                   /* the right part's first key 1..26 */
    uint8_t scale_l, scale_r;            /* 0 CHROMATIC, else SCALES[n - 1] */
    uint8_t key;                         /* the scales' key (C .. B), when the right hand does not follow */
    uint8_t follow;                      /* FOLLOW_* */
    uint8_t lowhold;                     /* the low split's notes stay */
    int8_t balance;                      /* -50 (the left only) .. +50 (the right only) */
} settings_t;
_Static_assert(sizeof(settings_t) <= PLAT_STORE_MAX, "the settings outgrew a store object");
/* the song: four tracks, each its own sound (a preset, as edited), its length, rate, transpose, mute; their steps.
 * The effects are the song's: the same in every track's sound, from track 1's preset (or as turned on the FX pages) */
typedef struct {
    patch_t snd;                         /* its sound, as edited */
    uint8_t pre;                         /* the preset it came from (SAVE's slot, HOME's revert) */
    uint8_t len, rate, mute;             /* 1..64 steps, RATE_*, muted */
    int8_t tr;                           /* transpose -24..+24 */
    uint8_t edited;                      /* its sound changed since the preset */
    uint8_t rsv[2];
} track_t;
typedef struct {
    uint32_t magic;
    uint8_t trk;                         /* the track selected: the keys play it, the pages edit it, REC writes it */
    uint8_t rsv[3];
    track_t t[NTRK];
    seq_t s;
} seqrec_t;
_Static_assert(sizeof(seqrec_t) <= PLAT_STORE_MAX, "the song outgrew a store object");

static settings_t set;
static seqrec_t sq;
#define ed (sq.t[sq.trk].snd)            /* the selected track's sound, as edited (what the pages show and turn) */
#define edited (sq.t[sq.trk].edited)
#define CUR (sq.t[sq.trk].pre)           /* its preset */
static void song_load(void);
static uint8_t set_dirty, seq_dirty, save_now;
static uint32_t silent_since, save_at;
static uint32_t dirty_at, seq_dirty_at;

static const uint8_t WK[16] = PLAT_WHITE_KEYS;
static const char *const NOTE_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const RATE_NAME[NRATE] = {"1/8", "1/8T", "1/16", "1/16T", "1/32"};
static const char *const ARP_NAME[NARP] = {"UP", "DOWN", "UP-DN", "RANDOM", "PLAYED"};
#define KEY_BASE 53                      /* the lowest key, F3, at octave 0 */
/* the keys' and MIDI's notes: their ids are the notes (engine.c's SRC_KEY) on the track they were played on (the
 * track selected then: a note let go after the track changed still finds its voice) */
static volatile uint8_t note_trk[128];
#define SRC_KEY_ID(n) ((n) | TRK_ID(note_trk[(n) & 127]))
static float u7f(int v) { return (float)v * (1.0f / 127.0f); }
static uint32_t text_len_(const char *s)  /* (no libc strlen on the device) */
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}

static void click_update(void);
static void sound_globals(void)
{
    eng_tempo((float)set.bpm, (float)set.swing * 0.01f);
    eng_arp(eng_arp_on(), set.arp_mode, set.arp_rate, set.arp_oct, (float)set.arp_gate * 0.01f);
    eng_arp_hold(set.arp_hold);
    {
        int t;
        for (t = 0; t < NTRK; t++)
            eng_trk_set(t, sq.t[t].len, sq.t[t].rate, sq.t[t].tr, sq.t[t].mute);
    }
    eng_view(sq.trk);
    eng_arp_track(sq.trk);
    click_update();
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
    set.kvel = 0;                        /* TOUCH: the keys' own velocity */
    set.arp_mode = ARP_UP;
    set.arp_rate = RATE_16;
    set.arp_oct = 1;
    set.arp_gate = 50;
    set.seq_bars = 1;
    set.seq_rate = RATE_16;
    set.seq_gate = 60;
    set.split_key = 7;                   /* F3..B3 left, C4 the right's first (as OFFWORLD) */
    set.follow = FOLLOW_ROOT;
}

static int settings_ok(void)
{
    uint32_t i, k;
    if (set.magic != SET_MAGIC || set.cur >= NPRESETS || set.oct < -3 || set.oct > 3 || set.bpm < 30 || set.bpm > 300
        || set.swing > 50 || set.kvel > 127 || set.mch > 16 || set.arp_mode >= NARP
        || set.arp_rate >= NRATE || set.arp_oct < 1 || set.arp_oct > 4 || set.arp_gate < 5 || set.arp_gate > 100
        || set.seq_bars < 1 || set.seq_bars > OLD_BARS || set.seq_rate >= NRATE || set.seq_gate < 5
        || set.seq_gate > 100 || set.seq_tr < -24 || set.seq_tr > 24 || set.split_oct > 3 || set.split_key < 1
        || set.split_key >= NKEYS || set.scale_l > NSCALE || set.scale_r > NSCALE || set.key > 11
        || set.follow >= NFOLLOW || set.lowhold > 1 || set.balance < -50 || set.balance > 50)
        return 0;
    for (i = 0; i < NPRESETS; i++)
        for (k = 0; k < NPARAM; k++)
            if (set.pre[i].p[k] > PARAMS[k].max)
                return 0;
    return 1;
}

/* an older record, from its bytes: magic, 16 presets of NAME_LEN + np bytes, then the globals (cur, oct, bpm (16 bits),
 * swing .. seq_tr) as they still are; what came later at its start */
static int settings_old(const uint8_t *raw, int n)
{
    uint32_t magic = (uint32_t)raw[0] | (uint32_t)raw[1] << 8 | (uint32_t)raw[2] << 16 | (uint32_t)raw[3] << 24;
    uint32_t np, off, i, k;
    const uint8_t *g;
    if (magic == SET_MAGIC_V1)
        np = 54;
    else if (magic == SET_MAGIC_V4)
        np = 66;
    else if (magic == SET_MAGIC_V2 || magic == SET_MAGIC_V3)
        np = 62;
    else
        return 0;
    off = 4u + NPRESETS * (NAME_LEN + np);
    if ((uint32_t)n < off + (magic == SET_MAGIC_V4 ? 27u : 19u))
        return 0;
    settings_defaults();
    for (i = 0; i < NPRESETS; i++) {
        const uint8_t *pr = raw + 4u + i * (NAME_LEN + np);
        for (k = 0; k < NAME_LEN; k++)
            set.pre[i].name[k] = (char)pr[k];
        for (k = 0; k < NPARAM; k++)
            set.pre[i].p[k] = k < np ? pr[NAME_LEN + k] : FACTORY[NPRESETS - 1].p[k];   /* (INIT's: AFTERTOUCH on) */
        if (magic != SET_MAGIC_V3)       /* CUTOFF to its finer steps */
            set.pre[i].p[P_CUTOFF] = (uint8_t)(pr[NAME_LEN + P_CUTOFF] >= 127 ? 255 : pr[NAME_LEN + P_CUTOFF] * 2);
    }
    g = raw + off;
    set.cur = g[0];
    set.oct = (int8_t)g[1];
    set.bpm = (uint16_t)(g[2] | g[3] << 8);
    set.swing = g[4];
    set.kvel = g[5];
    set.mch = g[6];
    set.arp_mode = g[7];
    set.arp_rate = g[8];
    set.arp_oct = g[9];
    set.arp_gate = g[10];
    set.arp_hold = g[11];
    set.seq_bars = g[12];
    set.seq_rate = g[13];
    set.seq_gate = g[14];
    set.seq_tr = (int8_t)g[15];
    if (magic != SET_MAGIC_V4)
        set.kvel = 0;                    /* (v0.5: the keys' own velocity) */
    else {                               /* v0.5's LOW SPLIT, after the globals' 3 spare bytes */
        set.split_oct = g[19];
        set.split_key = g[20];
        set.scale_l = g[21];
        set.scale_r = g[22];
        set.key = g[23];
        set.follow = g[24];
        set.lowhold = g[25];
        set.balance = (int8_t)g[26];
    }
    set.magic = SET_MAGIC;
    if (!settings_ok()) {
        settings_defaults();
        return 0;
    }
    set_dirty = 1;                       /* (kept in the new form soon) */
    return 1;
}

static void settings_load(void)
{
    static uint8_t raw[PLAT_STORE_MAX];
    int n = plat_store_load(OBJ_SETTINGS, raw, sizeof raw);
    if (n == (int)sizeof set) {
        uint8_t *d = (uint8_t *)&set;
        int i;
        for (i = 0; i < n; i++)
            d[i] = raw[i];
    }
    if ((n != (int)sizeof set || !settings_ok()) && !(n > 4 && settings_old(raw, n)))
        settings_defaults();
    song_load();
}

/* a fresh song: track 1 the preset playing, then a bass, a pluck and a lead, their lines empty, a bar each */
static void song_defaults(void)
{
    static const uint8_t PRE[NTRK] = {0, 3}; /* (POLY PAD, FAT BASS) */
    int t, k;
    memset(&sq, 0, sizeof sq);
    sq.magic = SONG_MAGIC;
    for (t = 0; t < NTRK; t++) {
        sq.t[t].pre = t == 0 ? set.cur : PRE[t];
        sq.t[t].snd = set.pre[sq.t[t].pre];
        sq.t[t].len = 16;
        sq.t[t].rate = RATE_16;
        if (t)                           /* the effects: track 1's, in every sound */
            for (k = 0; k < NPARAM; k++)
                if (FX_PARAM(k))
                    sq.t[t].snd.p[k] = sq.t[0].snd.p[k];
    }
}

static int song_ok(void)
{
    int t, k, i;
    if (sq.magic != SONG_MAGIC || sq.trk >= NTRK)
        return 0;
    for (t = 0; t < NTRK; t++) {
        const track_t *tr = &sq.t[t];
        if (tr->pre >= NPRESETS || tr->len < 1 || tr->len > TRK_STEPS || tr->rate >= NRATE || tr->mute > 1 || tr->tr < -24
            || tr->tr > 24)
            return 0;
        for (k = 0; k < NPARAM; k++)
            if (tr->snd.p[k] > PARAMS[k].max)
                return 0;
        for (i = 0; i < TRK_STEPS; i++)
            if (sq.s.st[t][i].vel > 127 || sq.s.st[t][i].gate > 100)
                return 0;
    }
    return 1;
}

/* the song as kept; or a 4-track test build's (its first two tracks); or v1.0's sequence, into track 1 (its first 64
 * steps, its bars, rate, gate, transpose) */
static void song_load(void)
{
    static uint8_t raw[PLAT_STORE_MAX];
    int n = plat_store_load(OBJ_SEQ, raw, sizeof raw);
    uint32_t magic = n >= 4 ? (uint32_t)raw[0] | (uint32_t)raw[1] << 8 | (uint32_t)raw[2] << 16 | (uint32_t)raw[3] << 24 : 0;
    if (n == (int)sizeof sq) {
        memcpy(&sq, raw, sizeof sq);
        if (song_ok())
            return;
    }
    song_defaults();
    if (magic == SONG_MAGIC_4 && n == (int)(8 + 4 * sizeof(track_t) + 4 * TRK_STEPS * sizeof(step_t))) {
        uint8_t trk = raw[4];
        memcpy(sq.t, raw + 8, sizeof sq.t);
        memcpy(sq.s.st, raw + 8 + 4 * sizeof(track_t), sizeof sq.s.st);
        sq.magic = SONG_MAGIC;
        sq.trk = (uint8_t)(trk < NTRK ? trk : 0);
        if (!song_ok())
            song_defaults();
        seq_dirty = 1;
        return;
    }
    if (n == 4 + OLD_BARS * 16 * SEQ_POLY && magic == SEQ_MAGIC_V1) {
        int i, k, len = set.seq_bars * 16;
        for (i = 0; i < TRK_STEPS; i++) {
            for (k = 0; k < SEQ_POLY; k++)
                sq.s.st[0][i].note[k] = raw[4 + i * SEQ_POLY + k];
            sq.s.st[0][i].gate = set.seq_gate;
        }
        sq.t[0].len = (uint8_t)(len > TRK_STEPS ? TRK_STEPS : len);
        sq.t[0].rate = set.seq_rate;
        sq.t[0].tr = set.seq_tr;
        seq_dirty = 1;                   /* (kept in the new form soon) */
    }
}

static void seq_changed(void);

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

/* the selected track's sound changed: the engine hears it, the song keeps it */
static void sound_apply(void)
{
    eng_patch_trk(sq.trk, ed.p);
    seq_changed();
}

/* a preset into the selected track. Into track 1: its effects become the song's (every track's); into another: the
 * song's effects stay */
static void preset_load(int i)
{
    int t, k;
    CUR = (uint8_t)i;
    set.cur = (uint8_t)i;
    ed = set.pre[i];
    edited = 0;
    for (k = 0; k < NPARAM; k++)
        if (FX_PARAM(k)) {
            if (sq.trk == 0)
                for (t = 1; t < NTRK; t++)
                    sq.t[t].snd.p[k] = ed.p[k];
            else
                ed.p[k] = sq.t[0].snd.p[k];
        }
    if (sq.trk == 0)
        for (t = 1; t < NTRK; t++)
            eng_patch_trk(t, sq.t[t].snd.p);
    sound_apply();
}

/* every track's sound to the engine (the start, a backup restored) */
static void song_apply(void)
{
    int t;
    for (t = NTRK - 1; t >= 0; t--)      /* (track 1 last: its effects) */
        eng_patch_trk(t, sq.t[t].snd.p);
    eng_seq_bind(&sq.s);
}

static void latched_release(uint32_t keys_down);
static void latch_down(void);
static void track_select(int t);

/* a step's velocity and gate as heard (0 in the record: the defaults), its lowest note (0: none) */
static int step_vel(const step_t *sp) { return sp->vel ? sp->vel : 100; }
static int step_gate(const step_t *sp) { return sp->gate ? sp->gate : 50; }
static int step_low(const step_t *sp)
{
    int k, lo = 0;
    for (k = 0; k < SEQ_POLY; k++)
        if (sp->note[k] >= 12 && (lo == 0 || sp->note[k] < lo))
            lo = sp->note[k];
    return lo;
}

/* -------------------------------------------------------------------- pages --- */
/* a knob: a parameter of the preset (0 .. NPARAM-1), or one of these (the settings, kept across presets) */
enum { G_BPM = 0x100, G_SWING, G_KVEL, G_MCH, G_ARP_MODE, G_ARP_RATE, G_ARP_OCT, G_ARP_GATE, G_TRK, G_TLEN, G_TRATE,
       G_TTR, G_SPLIT, G_POINT, G_SCALE_L, G_SCALE_R, G_KEY, G_FOLLOW, G_LOWHOLD, G_BALANCE, G_MUTE1, G_MUTE2, G_MUTE3,
       G_MUTE4, G_STEP, G_SVEL, G_SGATE, G_SNOTE, G_CLICK, G_CLVOL };
enum { PG_FILTER, PG_FILTER2, PG_OSC1, PG_OSC2, PG_MIXER, PG_FENV, PG_AENV, PG_LFO, PG_POLY, PG_FXA, PG_FXB, PG_SPACE,
       PG_VOICE, PG_CLOCK, PG_ARP, PG_SEQ, PG_LFO2, PG_LSYNC, PG_SPLIT, PG_SPLIT2, PG_AT, PG_DELAY, PG_DMOD, PG_MUTE,
       PG_STEP, PG_CLICK, NPAGES };
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
    [PG_POLY] = {"POLY MOD", B_EDIT, PG_SPLIT, {P_PM_FENV, P_PM_OSC2, P_PM_DOSC, P_PM_DFILT}},
    [PG_SPLIT] = {"LOW SPLIT", B_EDIT, PG_SPLIT2, {G_SPLIT, G_POINT, G_SCALE_L, G_SCALE_R}},
    [PG_SPLIT2] = {"SPLIT KEYS", B_EDIT, PG_POLY, {G_KEY, G_FOLLOW, G_LOWHOLD, G_BALANCE}},
    [PG_FXB] = {"REVERB", B_FX, PG_SPACE, {P_RV_TYPE, P_RV_MIX, P_RV_DECAY, P_RV_TONE}},
    [PG_SPACE] = {"SPACE REVERB", B_FX, PG_FXA, {P_SP_MOD, P_SP_SHIM, P_SP_SWELL, P_SP_PRE}},
    [PG_FXA] = {"CHORUS", B_FX, PG_DELAY, {P_CH_MIX, P_CH_RATE, P_CH_DEPTH, P_DIST}},
    [PG_DELAY] = {"DELAY", B_FX, PG_DMOD, {P_DL_TIME_L, P_DL_TIME_R, P_DL_FB, P_DL_MIX}},
    [PG_DMOD] = {"DELAY MOD", B_FX, PG_FXB, {P_DL_DEPTH, P_DL_RATE, P_DL_TONE, P_DL_SYNC}},
    [PG_VOICE] = {"VOICE", B_GLO, PG_AT, {P_GLIDE, P_AVEL, P_UNISON, P_BEND}},
    [PG_AT] = {"AFTERTOUCH", B_GLO, PG_CLOCK, {P_AT_DEST, P_AT_AMT, P_AT_CURVE, P_AT_SMOOTH}},
    [PG_CLOCK] = {"CLOCK", B_GLO, PG_CLICK, {G_BPM, G_SWING, G_KVEL, G_MCH}},
    [PG_CLICK] = {"METRONOME", B_GLO, PG_VOICE, {G_CLICK, G_CLVOL, G_BPM, G_SWING}},
    [PG_ARP] = {"ARPEGGIATOR", B_ARP, PG_ARP, {G_ARP_MODE, G_ARP_RATE, G_ARP_OCT, G_ARP_GATE}},
    [PG_SEQ] = {"TRACK", B_SEQ, PG_MUTE, {G_TRK, G_TLEN, G_TRATE, G_TTR}},
    [PG_MUTE] = {"MUTES & CLICK", B_SEQ, PG_SEQ, {G_MUTE1, G_MUTE2, G_CLICK, G_CLVOL}},
    [PG_STEP] = {"STEP EDIT", B_SEQ, PG_SEQ, {G_STEP, G_SVEL, G_SGATE, G_SNOTE}},
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
    uint8_t chord[SEQ_POLY], chord_n, chord_vel;
    uint8_t last_vel, last_gate;         /* step edit: what the last step was given (new steps take it) */
    uint8_t rec_held;                    /* step record: fingers down (keys and MIDI) */
    uint32_t home_t, rec_t, arp_t, seq_t;   /* when HOME, REC, ARP, SEQ went down (0: not, or used) */
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
    uint8_t sel_used, prs_all;
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
    case G_ARP_RATE: case G_TRATE: return NRATE - 1;
    case G_ARP_OCT: return 4;
    case G_ARP_GATE: case G_SGATE: return 100;
    case G_TRK: return NTRK;
    case G_TLEN: return TRK_STEPS;
    case G_TTR: return 24;
    case G_MUTE1: case G_MUTE2: case G_MUTE3: case G_MUTE4: return 1;
    case G_STEP: return sq.t[sq.trk].len;
    case G_CLICK: return 2;
    case G_CLVOL: return 100;
    case G_SPLIT: return 3;
    case G_POINT: return NKEYS - 1;
    case G_SCALE_L: case G_SCALE_R: return NSCALE;
    case G_KEY: return 11;
    case G_FOLLOW: return NFOLLOW - 1;
    case G_LOWHOLD: return 1;
    case G_BALANCE: return 50;
    }
    return 127;
}
static int g_min(int code)
{
    switch (code) {
    case G_BPM: return 30;
    case G_ARP_OCT: case G_TRK: case G_TLEN: case G_POINT: case G_STEP: case G_SVEL: return 1;
    case G_BALANCE: return -50;
    case G_ARP_GATE: case G_SGATE: return 5;
    case G_TTR: return -24;
    case G_SNOTE: return 12;
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
    case G_TRK: return sq.trk + 1;
    case G_TLEN: return sq.t[sq.trk].len;
    case G_TRATE: return sq.t[sq.trk].rate;
    case G_TTR: return sq.t[sq.trk].tr;
    case G_MUTE1: case G_MUTE2: case G_MUTE3: case G_MUTE4: return sq.t[code - G_MUTE1].mute;
    case G_STEP: return ui.cursor + 1;
    case G_SVEL: return step_vel(&sq.s.st[sq.trk][ui.cursor]);
    case G_SGATE: return step_gate(&sq.s.st[sq.trk][ui.cursor]);
    case G_SNOTE: return step_low(&sq.s.st[sq.trk][ui.cursor]);
    case G_CLICK: return set.click > 2 ? 0 : set.click;
    case G_CLVOL: return set.click_vol ? set.click_vol : 60;
    case G_SPLIT: return set.split_oct;
    case G_POINT: return set.split_key;
    case G_SCALE_L: return set.scale_l;
    case G_SCALE_R: return set.scale_r;
    case G_KEY: return set.key;
    case G_FOLLOW: return set.follow;
    case G_LOWHOLD: return set.lowhold;
    case G_BALANCE: return set.balance;
    }
    return 0;
}
static void knob_put(int code, int v)
{
    if (code < NPARAM) {
        /* OSC 2 at LFO rates (LOW FREQ, LF FIXED) is a modulation source, not heard: said when it goes there, and when
         * its mixer level is turned there */
        int lf_was = ed.p[P_O2_MODE] == O2M_LOW || ed.p[P_O2_MODE] == O2M_LOWFIX;
        int lf_now = code == P_O2_MODE ? (v == O2M_LOW || v == O2M_LOWFIX) : lf_was;
        if ((code == P_O2_MODE && lf_now && !lf_was) || (code == P_MIX_O2 && lf_now && !ui.msg_until))
            app_say("OSC 2 IS AN LFO NOW", "NOT HEARD: USE POLY MOD");
        ed.p[code] = (uint8_t)v;
        edited = 1;
        if (FX_PARAM(code)) {            /* the effects: the song's, in every track's sound */
            int t;
            for (t = 0; t < NTRK; t++)
                sq.t[t].snd.p[code] = (uint8_t)v;
            eng_patch_trk(0, sq.t[0].snd.p);
        }
        sound_apply();
        return;
    }
    if (code >= G_TRK && code <= G_TTR) {
        track_t *tr = &sq.t[sq.trk];
        if (code == G_TRK)
            track_select(v - 1);
        else if (code == G_TLEN)
            tr->len = (uint8_t)v;
        else if (code == G_TRATE)
            tr->rate = (uint8_t)v;
        else
            tr->tr = (int8_t)v;
        if (ui.cursor >= sq.t[sq.trk].len)
            ui.cursor = (int16_t)(sq.t[sq.trk].len - 1);
        sound_globals();
        seq_changed();
        return;
    }
    if (code >= G_MUTE1 && code <= G_MUTE4) {
        sq.t[code - G_MUTE1].mute = (uint8_t)v;
        sound_globals();
        seq_changed();
        return;
    }
    if (code >= G_STEP && code <= G_SNOTE) {
        step_t *sp = &sq.s.st[sq.trk][ui.cursor];
        if (code == G_STEP) {
            ui.cursor = (int16_t)(v - 1);
        } else if (code == G_SVEL) {
            sp->vel = (uint8_t)v;
            ui.last_vel = (uint8_t)v;
        } else if (code == G_SGATE) {
            sp->gate = (uint8_t)v;
            ui.last_gate = (uint8_t)v;
        } else {                         /* NOTE: the step's notes moved together (its lowest to here) */
            int d = v - step_low(sp), k;
            for (k = 0; k < SEQ_POLY; k++)
                if (sp->note[k] >= 12 && sp->note[k] + d >= 12 && sp->note[k] + d <= 127)
                    sp->note[k] = (uint8_t)(sp->note[k] + d);
        }
        if (code != G_STEP)
            seq_changed();
        ui.redraw = 1;
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
    case G_SPLIT: set.split_oct = (uint8_t)v; break;
    case G_POINT: set.split_key = (uint8_t)v; break;
    case G_SCALE_L: set.scale_l = (uint8_t)v; break;
    case G_SCALE_R: set.scale_r = (uint8_t)v; break;
    case G_KEY: set.key = (uint8_t)v; break;
    case G_FOLLOW: set.follow = (uint8_t)v; break;
    case G_LOWHOLD:
        set.lowhold = (uint8_t)v;
        if (!v)
            latched_release(ui.keys);    /* off: what only it held goes */
        else
            latch_down();                /* on: the left keys down now stay */
        break;
    case G_BALANCE: set.balance = (int8_t)v; break;
    case G_CLICK: set.click = (uint8_t)v; break;
    case G_CLVOL: set.click_vol = (uint8_t)(v ? v : 1); break;
    }
    sound_globals();
    dirty();
}
static const char *knob_name(int code)
{
    static const char *const G[] = {"BPM", "SWING", "KEY VEL", "MIDI CH", "MODE", "RATE", "OCTAVES", "GATE", "TRACK",
                                    "LENGTH", "RATE", "TRANSPOSE", "SPLIT", "POINT", "SCALE L", "SCALE R", "KEY",
                                    "FOLLOW", "LOW HOLD", "BALANCE", "TRACK 1", "TRACK 2", "TRACK 3", "TRACK 4", "STEP",
                                    "VELOCITY", "GATE", "NOTE", "CLICK", "VOLUME"};
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

static int dl_synced(int code) { return (code == P_DL_TIME_L || code == P_DL_TIME_R) && ed.p[P_DL_SYNC]; }

static void knob_text(int code, char *b)
{
    int v = knob_get(code);
    b[0] = 0;
    if (dl_synced(code)) {               /* synced: the note */
        text_cat(b, DL_SYNC_NAME[v * NDLS / 128]);
        return;
    }
    if (rate_sync(code) >= 0) {
        param_text(rate_sync(code), ed.p[rate_sync(code)], b);
        return;
    }
    if (code < NPARAM) {
        param_text(code, v, b);
        return;
    }
    switch (code) {
    case G_SWING: case G_ARP_GATE:
        int_text(v, b);
        text_cat(b, "%");
        break;
    case G_SGATE:
        if (sq.s.st[sq.trk][ui.cursor].note[0] == SN_TIE)
            text_cat(b, "TIE");
        else {
            int_text(v, b);
            text_cat(b, "%");
        }
        break;
    case G_SNOTE:
        if (v < 12)
            text_cat(b, "--");
        else
            note_text(v, b);
        break;
    case G_MUTE1: case G_MUTE2: case G_MUTE3: case G_MUTE4: text_cat(b, v ? "MUTE" : "ON"); break;
    case G_CLICK: text_cat(b, v == 1 ? "ON" : v == 2 ? "REC" : "OFF"); break;
    case G_CLVOL:
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
    case G_ARP_RATE: case G_TRATE: text_cat(b, RATE_NAME[v]); break;
    case G_TTR: case G_BALANCE:
        if (v > 0)
            text_cat(b, "+");
        int_text(v, b + (v > 0));
        break;
    case G_KVEL:
        if (v == 0)
            text_cat(b, "TOUCH");
        else
            int_text(v, b);
        break;
    case G_SPLIT:
        if (v == 0)
            text_cat(b, "OFF");
        else {
            text_cat(b, "-");
            int_text(v, b + 1);
            text_cat(b, " OCT");
        }
        break;
    case G_POINT: note_text(KEY_BASE + v + 12 * set.oct, b); break;
    case G_SCALE_L: case G_SCALE_R: text_cat(b, v ? SCALES[v - 1].name : "CHROMA"); break;
    case G_KEY: text_cat(b, NOTE_NAME[v]); break;
    case G_FOLLOW: text_cat(b, FOLLOW_NAME[v]); break;
    case G_LOWHOLD: text_cat(b, v ? "ON" : "OFF"); break;
    default: int_text(v, b);
    }
}
static int knob_max(int code) { return code < NPARAM ? PARAMS[code].max : g_max(code); }
static int knob_min(int code) { return code < NPARAM ? 0 : g_min(code); }
static int knob_choice(int code)         /* one detent, one value (no acceleration) */
{
    if (code < NPARAM)
        return PARAMS[code].kind == PK_CHOICE || PARAMS[code].max <= 48;
    return code != G_BPM && code != G_KVEL && code != G_SWING && code != G_ARP_GATE && code != G_SGATE
           && code != G_BALANCE && code != G_SVEL && code != G_CLVOL;
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

/* CUTOFF's 256 steps: slowly half a semitone a detent (precise), a quick turn sweeps the whole range in a flick */
static int32_t accel_fine(int enc, int32_t e)
{
    uint32_t now = plat_ms(), dt = now - ui.enc_at[enc];
    ui.enc_at[enc] = now;
    if (dt < 30u)
        return e * 10;
    if (dt < 55u)
        return e * 6;
    if (dt < 100u)
        return e * 3;
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
    if (dl_synced(code)) {               /* synced: a detent, the next note */
        int idx = v * NDLS / 128 + (e > 0 ? 1 : -1);
        if (idx >= 0 && idx < NDLS)
            knob_put(code, idx * 128 / NDLS + 5);
        ui.redraw = 1;
        return;
    }
    if (!knob_choice(code))
        e = mx > 127 ? accel_fine(EN_K1 + k, e) : accel(EN_K1 + k, e);
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
/* (the selected track's) */
static int seq_len(void) { return sq.t[sq.trk].len; }
static step_t *step_at(int st) { return &sq.s.st[sq.trk][st]; }

static void step_write(int st, const uint8_t *notes, int n, int vel)
{
    step_t *sp = step_at(st);
    int i;
    for (i = 0; i < SEQ_POLY; i++)
        sp->note[i] = (uint8_t)(i < n ? notes[i] : SN_NONE);
    if (vel > 0)
        sp->vel = (uint8_t)vel;
    if (!sp->gate)
        sp->gate = ui.last_gate ? ui.last_gate : 50;
    seq_changed();
}

static void step_add(int st, int note, int vel)   /* live: a note into a step (beside what is there) */
{
    step_t *sp = step_at(st);
    int i, empty = step_low(sp) == 0;
    if (sp->note[0] == SN_TIE)
        sp->note[0] = SN_NONE;
    for (i = 0; i < SEQ_POLY; i++)
        if (sp->note[i] == note)
            return;
    for (i = 0; i < SEQ_POLY; i++)
        if (sp->note[i] < 12) {
            sp->note[i] = (uint8_t)note;
            if (empty || vel > sp->vel)
                sp->vel = (uint8_t)vel;
            if (!sp->gate)
                sp->gate = ui.last_gate ? ui.last_gate : 50;
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

/* live: each note's step and when it went down, so its length is recorded as played: within a step its gate; longer,
 * TIEs over the steps it holds on (as far as they are free), the last one's gate where it was let go */
static const uint8_t RATE_TK[NRATE] = {12, 8, 6, 4, 3};
static int16_t live_st[128];             /* (-1: none; app_init) */
static float live_t0[128];
static uint8_t live_fresh[128];

static void live_up(int note)
{
    int st = live_st[note & 127], len = seq_len(), n, i;
    float L = (float)RATE_TK[sq.t[sq.trk].rate], d = eng_clock_now() - live_t0[note & 127], steps;
    step_t *sp;
    live_st[note & 127] = -1;
    if (st < 0 || st >= len)
        return;
    steps = d / L;
    n = (int)(steps + 0.95f);            /* the steps it reaches into (a hair short of one: still that one) */
    if (n < 1)
        n = 1;
    if (n > len - 1)
        n = len - 1 > 0 ? len - 1 : 1;
    sp = step_at(st);
    if (n == 1) {
        int g = (int)(steps * 100.0f + 0.5f);
        g = g < 5 ? 5 : g > 100 ? 100 : g;
        if (live_fresh[note & 127] || g > sp->gate)
            sp->gate = (uint8_t)g;
    } else {
        sp->gate = 100;
        for (i = 1; i < n; i++) {
            step_t *tp = step_at((st + i) % len);
            float rest = steps - (float)i;
            int g = (int)(rest * 100.0f + 0.5f);
            if (step_low(tp) && tp->note[0] != SN_TIE)
                break;                   /* a step with notes of its own: the held note stops there */
            memset(tp->note, 0, sizeof tp->note);
            tp->note[0] = SN_TIE;
            tp->gate = (uint8_t)(i < n - 1 || g > 100 ? 100 : g < 5 ? 5 : g);
        }
    }
    seq_changed();
}

/* a note down / up for the recording (keys and MIDI): live into the nearest step, or a step's chord */
static void rec_down(int note, int vel)
{
    if (!ui.rec)
        return;
    if (eng_playing()) {
        int st = eng_seq_nearest(sq.trk);
        if (st >= 0) {
            live_fresh[note & 127] = (uint8_t)(step_low(step_at(st)) == 0);
            step_add(st, note, vel);
            live_st[note & 127] = (int16_t)st;
            live_t0[note & 127] = eng_clock_now();
        }
        return;
    }
    if (ui.rec_held == 0) {
        ui.chord_n = 0;
        ui.chord_vel = 0;
    }
    ui.rec_held++;
    if (ui.chord_n < SEQ_POLY)
        ui.chord[ui.chord_n++] = (uint8_t)note;
    if (vel > ui.chord_vel)
        ui.chord_vel = (uint8_t)vel;
}

static void rec_up(int note)
{
    if (ui.rec && eng_playing()) {
        live_up(note);
        return;
    }
    if (!ui.rec || eng_playing() || ui.rec_held == 0)
        return;
    if (--ui.rec_held == 0 && ui.chord_n) {   /* every finger off: the chord into the step, on to the next */
        step_write(ui.cursor, ui.chord, ui.chord_n, ui.chord_vel);
        ui.chord_n = 0;
        cursor_move(1);
    }
}

/* MIDI's notes, for the recording (the tick puts them here, the main loop takes them) */
static volatile uint8_t mrec_note[32], mrec_on[32], mrec_vel[32];
static volatile uint32_t mrec_w, mrec_r;

/* MIDI CC: the knobs from a controller. The numbers are the Prophet-6's where it has the same knob (its templates and
 * editors work), the rest in the CCs it leaves free. 0..127 spans the knob (a choice: its options, evenly). The CC
 * edits the sound of the track on screen (the sequencer's track 1 or 2); 1 (mod wheel), 64 (sustain), 74 (brightness:
 * added to the cutoff) and 120/123 (all off) as before. Listed in docs/HANDBUCH.md and README.md. */
static const uint8_t CC_MAP[][2] = {
    {5, P_GLIDE},
    {8, P_O1_SUB},
    {9, P_DIST},
    {14, P_LFO_RATE},
    {15, P_LFO_AMT},
    {16, P_LFO_SHAPE},
    {17, P_LFO_DEST},
    {18, P_L2_RATE},
    {19, P_L2_AMT},
    {20, P_L2_SHAPE},
    {21, P_L2_DEST},
    {22, P_PM_FENV},
    {23, P_PM_OSC2},
    {24, P_PM_DOSC},
    {25, P_PM_DFILT},
    {26, P_NOISE},
    {27, P_O2_MODE},
    {28, P_VINTAGE},
    {29, P_SPREAD},
    {30, P_UNISON},
    {41, P_AVEL},
    {43, P_AA},
    {44, P_AD},
    {45, P_AS},
    {46, P_AR},
    {47, P_ENV_AMT},
    {50, P_FA},
    {51, P_FD},
    {52, P_FS},
    {53, P_FR},
    {54, P_HP_ENV},
    {67, P_O1_FREQ},
    {69, P_MIX_O1},
    {70, P_O1_SHAPE},
    {71, P_O1_PW},
    {75, P_O2_FREQ},
    {76, P_O2_FINE},
    {77, P_MIX_O2},
    {78, P_O2_SHAPE},
    {79, P_O2_PW},
    {80, P_AT_AMT},
    {81, P_AT_DEST},
    {85, P_CH_MIX},
    {86, P_CH_RATE},
    {87, P_CH_DEPTH},
    {88, P_RV_TYPE},
    {89, P_RV_MIX},
    {90, P_RV_DECAY},
    {102, P_CUTOFF},
    {103, P_RESO},
    {104, P_KEYTRK},
    {105, P_FVEL},
    {106, P_HPF},
    {107, P_HP_RESO},
    {110, P_RV_TONE},
    {111, P_SP_MOD},
    {112, P_SP_SHIM},
    {113, P_SP_SWELL},
    {114, P_SP_PRE},
    {115, P_DL_MIX},
    {116, P_DL_FB},
    {117, P_DL_TIME_L},
    {118, P_DL_TIME_R},
    {119, P_DL_TONE}
};
/* the MIDI's interrupt keeps a CC's latest value per knob; the main loop turns the knobs (a fast sweep: only its
 * newest value, once a loop) */
static volatile uint8_t cc_new[NPARAM];  /* 0..127, or 255: nothing new */
static int cc_param(int cc)
{
    uint32_t i;
    for (i = 0; i < sizeof CC_MAP / sizeof CC_MAP[0]; i++)
        if (CC_MAP[i][0] == cc)
            return CC_MAP[i][1];
    return -1;
}

/* ------------------------------------------------------------- the keys --- */
/* The LOW SPLIT (OFFWORLD's): the keys left of POINT play SPLIT octaves down, in SCALE L (on KEY); the keys from POINT
 * on play in SCALE R, on KEY, or (FOLLOW ROOT) on the root the left hand plays: the lowest note sounding there, or the
 * last one; WHITE lays that scale on the white keys from POINT up (the black keys rest). LOW HOLD: the left hand's
 * notes stay until a new note is played there. BALANCE: the two hands' levels. Without the split, SCALE R covers the
 * whole keyboard, on KEY. */
static int split_on(void) { return set.split_oct != 0u; }
static int is_left(uint32_t k) { return split_on() && k < set.split_key; }
static int is_white(uint32_t k)
{
    uint32_t i;
    for (i = 0; i < 16u; i++)
        if (WK[i] == k)
            return 1;
    return 0;
}
static int snap(int n, int root, int scale)  /* to the scale's note at or below n */
{
    const scale_t *sc = &SCALES[(scale - 1) % (int)NSCALE];
    int rel = ((n - root) % 12 + 12) % 12, best = 0, i;
    for (i = 0; i < sc->n; i++)
        if (sc->st[i] <= rel)
            best = sc->st[i];
    return n - rel + best;
}

static int8_t key_sounding[NKEYS];        /* the note each key started (-1 none) */
static uint32_t latched;                  /* LOW HOLD: the left keys whose notes stay */
static int root = -1;                     /* the left hand's root (a note class 0..11), -1 none yet */

static int key_map(uint32_t k)            /* the note key k plays now (-1: it rests) */
{
    int nat = KEY_BASE + (int)k + 12 * set.oct, base;
    if (is_left(k)) {
        int n = nat - 12 * set.split_oct;
        return set.scale_l ? snap(n, set.key, set.scale_l) : n;
    }
    if (!set.scale_r)
        return nat;
    base = split_on() && set.follow != FOLLOW_KEY && root >= 0 ? root : set.key;
    if (split_on() && set.follow == FOLLOW_WHITE) {
        const scale_t *sc = &SCALES[(set.scale_r - 1) % (int)NSCALE];
        uint32_t j, first = set.split_key, d = 0;
        int p0, b0;
        if (!is_white(k))
            return -1;
        while (first < NKEYS && !is_white(first))
            first++;
        for (j = first; j < k; j++)
            d += (uint32_t)is_white(j);
        p0 = KEY_BASE + (int)first + 12 * set.oct;
        b0 = p0 - (((p0 - base) % 12 + 12) % 12);
        if (p0 - b0 > 6)
            b0 += 12;
        return b0 + 12 * (int)(d / sc->n) + sc->st[d % sc->n];
    }
    return snap(nat, base, set.scale_r);
}

static void root_update(void)             /* the lowest note sounding in the left hand */
{
    uint32_t k;
    for (k = 0; k < NKEYS; k++)
        if (is_left(k) && key_sounding[k] >= 0) {
            if (root != key_sounding[k] % 12) {
                root = key_sounding[k] % 12;
                ui.redraw = 1;
            }
            return;
        }
}

static float zone_gain(uint32_t k)        /* BALANCE */
{
    float b = (float)set.balance * (1.0f / 50.0f);
    if (!split_on())
        return 1.0f;
    return is_left(k) ? (b > 0.0f ? 1.0f - b : 1.0f) : (b < 0.0f ? 1.0f + b : 1.0f);
}

static void note_down(int note, float vel, float gain, int from_keys)
{
    note_trk[note & 127] = sq.trk;
    if (eng_arp_on())
        eng_arp_key(note, 1, vel);
    else
        eng_note_on_gain(SRC_KEY_ID(note), note, vel, gain);
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

static void key_stop(uint32_t k)
{
    if (key_sounding[k] >= 0) {
        note_up(key_sounding[k], 1);
        rec_up(key_sounding[k]);
    }
    key_sounding[k] = -1;
    latched &= ~(1u << k);
}

static void latched_release(uint32_t keys_down)   /* LOW HOLD's notes not held go */
{
    uint32_t k;
    for (k = 0; k < NKEYS; k++)
        if ((latched >> k & 1u) && !(keys_down >> k & 1u))
            key_stop(k);
    latched = 0;
}

static uint8_t prs_sent[NKEYS];
static uint8_t sel_now;

static void latch_down(void)
{
    uint32_t k;
    for (k = 0; k < NKEYS; k++)
        if ((ui.keys >> k & 1u) && is_left(k) && key_sounding[k] >= 0)
            latched |= 1u << k;
}

static void keys_input(uint32_t keys)
{
    uint32_t k, left_down = 0;
    for (k = 0; k < NKEYS; k++)
        if ((ui.keys >> k & 1u) && is_left(k))
            left_down |= 1u << k;
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit)) {
            int n, v;
            if (sel_now) {               /* SEL held + a key: the split there (OFFWORLD's) */
                char t[8];
                if (k >= 1u) {
                    if (!set.split_oct)
                        set.split_oct = 1;
                    set.split_key = (uint8_t)k;
                    note_text(KEY_BASE + (int)k + 12 * set.oct, t);
                    app_say("SPLIT AT", t);
                    dirty();
                }
                ui.sel_used = 1;
                continue;
            }
            if (is_left(k) && set.lowhold && !left_down && latched) {
                latched_release(keys);   /* a new note on the left: the held ones go */
                if (key_sounding[k] >= 0)
                    key_stop(k);
            }
            if (key_sounding[k] >= 0)
                key_stop(k);
            n = key_map(k);
            if (n < 0 || n > 127)
                continue;
            v = set.kvel ? set.kvel : touch_vel(k);
            if (v <= 0)
                v = 100;
            key_sounding[k] = (int8_t)n;
            prs_sent[k] = 0;
            note_down(n, (float)v * (1.0f / 127.0f), zone_gain(k), 1);
            rec_down(n, v);
            if (is_left(k)) {
                left_down |= bit;
                if (set.lowhold)
                    latched |= bit;
                root_update();
            }
        } else if (!(keys & bit) && (ui.keys & bit)) {
            if (is_left(k))
                left_down &= ~bit;
            if ((latched >> k & 1u) && set.lowhold) {
                if (key_sounding[k] >= 0 && prs_sent[k]) {   /* it stays, no longer pressed */
                    eng_press(SRC_KEY_ID(key_sounding[k]), 0.0f);
                    prs_sent[k] = 0;
                }
                continue;
            }
            key_stop(k);
            root_update();
        }
    }
}

/* AFTERTOUCH: the held keys' pressure to their notes (with the arp on: the hardest pressed, to all) */
static void pressure_input(uint32_t keys)
{
    uint32_t k;
    uint8_t mx = 0;
    if (ed.p[P_AT_DEST] == AT_OFF)
        return;
    for (k = 0; k < NKEYS; k++) {
        uint8_t pr;
        int d;
        if ((keys >> k & 1u) && key_sounding[k] >= 0 && touch_sensed(k) && !touch_down(k))
            continue;                    /* lifting: its reading falls before the release; the note keeps what it had */
        pr = (keys >> k & 1u) && key_sounding[k] >= 0 ? touch_pressure(k) : 0u;
        d = (int)pr - (int)prs_sent[k];
        if (pr > mx)
            mx = pr;
        if (eng_arp_on())
            continue;
        if (key_sounding[k] < 0)         /* let go: the engine eases the pressure off with the release */
            continue;
        if (d >= 2 || d <= -2 || (pr == 0u && prs_sent[k])) {
            prs_sent[k] = pr;
            if (key_sounding[k] >= 0)
                eng_press(SRC_KEY_ID(key_sounding[k]), (float)pr * (1.0f / 127.0f));
        }
    }
    if (eng_arp_on() && (mx > ui.prs_all + 1u || mx + 1u < ui.prs_all || (mx == 0u && ui.prs_all))) {
        ui.prs_all = mx;
        eng_press_all((float)mx * (1.0f / 127.0f));
    }
}

/* the metronome: ON while the sequencer plays, REC only while recording */
static void click_update(void)
{
    int m = set.click > 2 ? 0 : set.click;
    eng_click(m == 1 || (m == 2 && ui.rec), (float)(set.click_vol ? set.click_vol : 60) * 0.01f);
}

/* a track selected: the keys play its sound from now (the notes held still let go on theirs), the pages show it, REC
 * writes into it, the arp plays it */
static void track_select(int t)
{
    char a[12];
    if (t < 0 || t >= NTRK || t == sq.trk)
        return;
    sq.trk = (uint8_t)t;
    set.cur = CUR;
    ui.chord_n = 0;
    if (ui.cursor >= seq_len())
        ui.cursor = 0;
    eng_view(t);
    eng_arp_track(t);
    seq_changed();
    a[0] = 0;
    text_cat(a, "TRACK ");
    int_text(t + 1, a + 6);
    app_say(a, ed.name);
}

/* ----------------------------------------------------------------- buttons --- */
static void preset_browse(int32_t e)
{
    int c = ui.save_down && ui.copy_to >= 0 ? ui.copy_to : CUR;
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
    click_update();
    ui.chord_n = ui.rec_held = 0;
    if (ui.rec) {
        if (!eng_playing()) {
            ui.cursor = 0;
            page_set(PG_STEP);
            app_say("STEP RECORD", "PLAY A CHORD");
        } else {
            app_say("RECORD", "LIVE");
        }
    } else {
        if (ui.page == PG_STEP)
            page_set(PG_SEQ);
        app_say("RECORD", "OFF");
    }
}

static void button(uint32_t i)
{
    const page_t *pg = &PAGES[ui.page];
    if (ui.rec && !eng_playing()) {      /* step record: SEQ a rest, SAVE a tie */
        if (i == B_SEQ) {
            step_write(ui.cursor, 0, 0, 0);
            cursor_move(1);
            app_say("REST", 0);
            return;
        }
        if (i == B_SAVE) {
            static const uint8_t tie[1] = {SN_TIE};
            step_write(ui.cursor, tie, 1, 0);
            cursor_move(1);
            app_say("TIE", 0);
            return;
        }
    }
    switch (i) {
    case B_HOME:
        ui.home_t = plat_ms();
        /* fall through */
    case B_SEQ:
        ui.seq_t = plat_ms();
        /* fall through */
    case B_SEL: case B_ENV: case B_LFO: case B_EDIT: case B_FX: case B_GLO: {
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
        int to = ui.copy_to >= 0 ? ui.copy_to : CUR;
        ui.save_down = 0;
        if (ui.rec && !eng_playing())
            return;
        set.pre[to] = ed;
        CUR = (uint8_t)to;
        set.cur = (uint8_t)to;
        seq_changed();
        edited = 0;
        save_now = 1;                    /* asked for: written at once, the sound faded out around it */
        app_say("SAVED", ed.name);
    }
    if (ui.home_t) {                     /* HOME held: the preset as it was saved */
        if (!(btn >> B_HOME & 1u))
            ui.home_t = 0;
        else if (now - ui.home_t >= 1000u) {
            ui.home_t = 0;
            preset_load(CUR);
            app_say("REVERT", ed.name);
        }
    }
    if (ui.rec_t) {                      /* REC held: the sequence cleared */
        if (!(btn >> B_REC & 1u))
            ui.rec_t = 0;
        else if (now - ui.rec_t >= 1000u) {
            ui.rec_t = 0;
            memset(sq.s.st[sq.trk], 0, sizeof sq.s.st[sq.trk]);   /* (the selected track's) */
            seq_changed();
            ui.rec = 0;
            ui.cursor = 0;
            app_say("TRACK", "CLEARED");
        }
    }
    if (ui.seq_t) {                      /* SEQ held: the metronome on / off */
        if (!(btn >> B_SEQ & 1u))
            ui.seq_t = 0;
        else if (now - ui.seq_t >= 1000u) {
            ui.seq_t = 0;
            set.click = set.click ? 0 : 1;
            click_update();
            dirty();
            app_say("METRONOME", set.click ? "ON" : "OFF");
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
    uint32_t keys, btn = plat_buttons(), i;
    int32_t e;
    touch_poll();                        /* the keys with their velocity and pressure */
    keys = touch_keys();
    sel_now = (uint8_t)(btn >> B_SEL & 1u);   /* (SEL held + a key: the split point) */
    keys_input(keys);
    pressure_input(keys);
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
    {                                    /* MIDI CCs: the knobs */
        int k;
        for (k = 0; k < NPARAM; k++) {
            uint8_t v = cc_new[k];
            if (v != 255u) {
                cc_new[k] = 255u;
                knob_put(k, ((int)v * PARAMS[k].max + 63) / 127);
                ui.redraw = 1;
            }
        }
    }
    while (mrec_r != mrec_w) {           /* MIDI notes for the recording */
        uint32_t k = mrec_r % 32u;
        if (mrec_on[k])
            rec_down(mrec_note[k], mrec_vel[k]);
        else
            rec_up(mrec_note[k]);
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
    (void)v;
    for (i = 0; i < NKEYS; i++) {        /* the keys sounding lit (LOW HOLD's too); the low split glowing */
        if (key_sounding[i] >= 0)
            lit |= 1u << i;
        else if (is_left(i))
            dim |= 1u << i;
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
            note_down((int)d1, (float)d2 * (1.0f / 127.0f), 1.0f, 0);
            if (mrec_w - mrec_r < 32u) {
                mrec_note[mrec_w % 32u] = (uint8_t)d1;
                mrec_vel[mrec_w % 32u] = (uint8_t)d2;
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
        } else if (type == 0xD0u) {        /* channel pressure: every note */
            eng_press_all((float)d1 * (1.0f / 127.0f));
        } else if (type == 0xA0u) {        /* poly pressure: the note's */
            eng_press(SRC_KEY_ID((int)d1), (float)d2 * (1.0f / 127.0f));
        } else if (type == 0xE0u) {
            eng_bend((float)((int)(d1 | d2 << 7) - 8192) * (1.0f / 8192.0f));
        } else if (type == 0xB0u) {
            int k = cc_param((int)d1);
            if (k >= 0)                    /* a knob, from a controller: the current track's sound */
                cc_new[k] = (uint8_t)d2;
            else if (d1 == 1u)
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

/* ---- the head: the name, the battery, the preset ---- */
/* the battery, right of the name (on every page: the head is): its bars (0..3), red and blinking when empty; a USB
 * host's power: a red bolt in it; not read yet (the emulator): nothing */
static void draw_battery(int32_t x, int32_t y)
{
    int b = plat_battery(), i;
    uint16_t c, cd;
    if (b < 0)
        return;
    c = cd = b == 0 ? K_RED : K_CREAM;   /* the frame light; empty: red, blinking */
    if (b == 0 && (plat_ms() / 500u) % 2u)
        c = cd = K_RED_D;
    /* the cell, 15 x 11, its corners rounded off; the tip on the right */
    cv_rect(x + 1, y, 13, 1, c);
    cv_rect(x + 1, y + 10, 13, 1, cd);
    cv_rect(x, y + 1, 1, 9, c);
    cv_rect(x + 14, y + 1, 1, 9, cd);
    cv_rect(x + 15, y + 3, 1, 5, cd);
    cv_rect(x + 16, y + 3, 1, 5, cd);
    cv_rect(x + 15, y + 3, 2, 1, c);
    if (b >= 4) {                        /* on USB: a bolt, filled */
        static const uint8_t BOLT[9][2] = {{7, 3}, {6, 3}, {5, 3}, {4, 3}, {3, 8}, {6, 3}, {5, 3}, {4, 3}, {4, 2}};
        static const int8_t BX[9] = {7, 6, 5, 4, 3, 6, 5, 4, 4};
        for (i = 0; i < 9; i++)
            cv_rect(x + 1 + BX[i], y + 1 + i, BOLT[i][1], 1, K_RED);
        return;
    }
    for (i = 0; i < 3; i++) {            /* the bars: lit (a light top, a shade under), or a dim outline */
        int32_t bx = x + 2 + i * 4;
        if (i < b) {
            uint16_t f = K_RED, fd = K_RED_M;   /* the bars red, as the panel's heads */
            cv_rect(bx, y + 2, 3, 1, f);
            cv_rect(bx, y + 3, 3, 5, f);
            cv_rect(bx, y + 8, 3, 1, fd);
        } else {
            cv_rect(bx, y + 2, 3, 7, K_DIM);
        }
    }
}

static void draw_head(void)
{
    char t[4];
    const felucca_font_t *nf;
    int32_t bx = 135, bw = 101, i, ax = bx + bw - 10;
    cv_text(7, 1, &FONT_M, "PROBLEM 6", K_CREAM);
    draw_battery(112, 7);
    cv_rect(131, 4, 1, 18, K_GREY);
    frame_rect(bx, 2, bw, 22, K_CREAM);
    frame_rect(bx + 1, 3, bw - 2, 20, K_DIM);
    t[0] = (char)('0' + (CUR + 1) / 10);   /* its slot, small and red, then its name */
    t[1] = (char)('0' + (CUR + 1) % 10);
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
        knob(cx, 125, 8, ed.p[KP[i]], PARAMS[KP[i]].max, 0);
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
            int lf = i == 1 && (ed.p[P_O2_MODE] == O2M_LOW || ed.p[P_O2_MODE] == O2M_LOWFIX);
            cv_text(bx, y + 2, &FONT_XS, lf ? "OSC 2 LFO" : L[i], lf ? K_RED : K_GREY);   /* (LFO: not heard) */
            cv_rect(bx, y + 16, bw, 8, K_CELL);
            cv_rect(bx + 1, y + 17, fill, 6, lf ? K_DIM : K_RED);
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
        cv_text(x + 12 + text_w(&FONT_L, t), y + 14, &FONT_B, eng_clock_ext() ? "EXT" : "BPM", K_RED);
        {                                /* the load of each core, now and its peak in the last second */
            uint32_t c1, c2, p1, p2;
            eng_load(&c1, &c2, &p1, &p2);
            t[0] = 0;
            text_cat(t, "C1 ");
            int_text((int32_t)c1, t + text_len_(t));
            text_cat(t, "% PK ");
            int_text((int32_t)p1, t + text_len_(t));
            cv_text(x + w - 92, y + 1, &FONT_XS, t, p1 >= 85u ? K_RED : K_GREY);
            t[0] = 0;
            text_cat(t, "C2 ");
            int_text((int32_t)c2, t + text_len_(t));
            text_cat(t, "% PK ");
            int_text((int32_t)p2, t + text_len_(t));
            cv_text(x + w - 92, y + 13, &FONT_XS, t, p2 >= 85u ? K_RED : K_GREY);
        }
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
    case PG_CLICK: {                     /* the bar's four beats, the one now lit */
        int b2, bt = eng_beat();
        for (b2 = 0; b2 < 4; b2++) {
            int32_t bx = x + 14 + b2 * 52;
            cv_rect(bx, y + 6, 40, 18, b2 == bt ? (b2 == 0 ? K_RED : K_CREAM) : K_CELL);
        }
        break;
    }
    case PG_SEQ:
    case PG_MUTE:
    case PG_STEP: {                      /* the two tracks: their steps (those with notes), their length, the playheads */
        int t2, i2;
        for (t2 = 0; t2 < NTRK; t2++) {
            int32_t ly = y + t2 * 15 + 2, lx = x + 16;
            int st = eng_seq_step(t2), len = sq.t[t2].len, sel = t2 == sq.trk, mute = sq.t[t2].mute;
            cv_rect(x + 4, ly, 8, 11, sel ? K_RED : mute ? K_PANEL : K_GREY);   /* its mark: selected, muted */
            if (mute)
                frame_rect(x + 4, ly, 8, 11, K_DIM);
            for (i2 = 0; i2 < TRK_STEPS; i2++) {
                const step_t *sp = &sq.s.st[t2][i2];
                uint16_t c = i2 >= len ? K_PANEL : sp->note[0] == SN_TIE ? K_RED_M : step_low(sp) ? (mute ? K_GREY : K_RED)
                                                                                        : K_CELL;
                if (i2 < len && i2 == st)
                    c = K_CREAM;
                cv_rect(lx + i2 * 3, ly, 2, 11, c);
            }
            if (sel)
                frame_rect(lx - 2, ly - 2, TRK_STEPS * 3 + 3, 15, K_GREY);
        }
        break;
    }
    case PG_SPLIT:
    case PG_SPLIT2: {                    /* the keyboard: the low split, the point, the keys the scales play */
        int32_t kx = x + 6, kw = (w - 12) / 16, j, wi = 0;
        for (j = 0; j < NKEYS; j++) {    /* the white keys first */
            if (!is_white((uint32_t)j))
                continue;
            {
                int n = key_map((uint32_t)j), nat = KEY_BASE + j + 12 * set.oct;
                uint16_t c = key_sounding[j] >= 0 ? K_RED : is_left((uint32_t)j) ? RGB(120, 40, 34)
                             : n < 0 ? K_DIM : n != nat ? K_GREY : K_CREAM;   /* (grey: it moves into the scale) */
                cv_rect(kx + wi * kw, y + 2, kw - 1, h - 6, c);
            }
            wi++;
        }
        wi = 0;
        for (j = 0; j < NKEYS; j++) {    /* the black keys over them */
            if (is_white((uint32_t)j)) {
                wi++;
                continue;
            }
            {
                int n = key_map((uint32_t)j);
                uint16_t c = key_sounding[j] >= 0 ? K_RED : is_left((uint32_t)j) ? RGB(70, 20, 18)
                             : n < 0 ? K_PANEL : RGB(40, 40, 44);
                cv_rect(kx + wi * kw - kw / 3, y + 2, kw * 2 / 3, (h - 6) * 3 / 5, c);
            }
        }
        if (split_on()) {                /* the point */
            int32_t px = kx, cnt = 0;
            for (j = 0; j < (int)set.split_key; j++)
                cnt += is_white((uint32_t)j);
            px += cnt * kw - 1;
            cv_rect(px, y, 2, h - 2, K_RED);
        }
        if (split_on() && set.follow != FOLLOW_KEY && set.scale_r) {   /* the root the right hand follows */
            char t[8] = "ROOT ";
            text_cat(t, root >= 0 ? NOTE_NAME[root] : NOTE_NAME[set.key]);
            cv_text(x + w - 60, y - 14, &FONT_XS, t, K_RED);
        }
        break;
    }
    case PG_DELAY:
    case PG_DMOD: {                      /* the repeats, left above, right below, over 2.4 s */
        int c, r;
        for (c = 0; c < 2; c++) {
            int v = ed.p[c ? P_DL_TIME_R : P_DL_TIME_L];
            float sec = ed.p[P_DL_SYNC] ? (float)DL_SYNC_TICKS[v * NDLS / 128] * 60.0f / (24.0f * eng_bpm())
                                        : 0.02f * fm_exp2f((float)v * (5.9069f / 127.0f));
            float g = 1.0f, fb = 1.08f * fm_powf(u7f(ed.p[P_DL_FB]), 0.8f);
            int32_t yb = y + (c ? h - 2 : h / 2 - 1);
            cv_text(x + 2, yb - 11, &FONT_XS, c ? "R" : "L", K_GREY);
            for (r = 1; r < 24; r++) {
                int32_t px = x + 14 + (int32_t)(sec * (float)r / 2.4f * (float)(w - 20)), hh;
                if (px > x + w - 4)
                    break;
                hh = (int32_t)(fm_minf(g, 1.0f) * (float)(h / 2 - 3));
                if (hh < 1)
                    break;
                cv_rect(px, yb - hh, 2, hh, ed.p[P_DL_MIX] ? K_RED : K_DIM);
                g *= fm_minf(fb, 1.0f);
            }
        }
        break;
    }
    case PG_AT: {                        /* each voice's pressure, as it reaches the sound */
        for (i = 0; i < NVOICE; i++) {
            int32_t bx = x + 14 + i * 30, hh = (int32_t)(eng_voice_press(i) * (float)(h - 6));
            cv_rect(bx, y + 2, 20, h - 6, K_CELL);
            cv_rect(bx, y + h - 4 - hh, 20, hh, K_RED);
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

/* ---- the sequencer's strip: the selected track, the sixteen steps playing (or being written): each step's velocity
 * its height, its gate its width ---- */
static void draw_seq(void)
{
    int st = eng_seq_step(sq.trk), page, i, len = seq_len();
    int writing = ui.rec && !eng_playing();
    char t[16];
    cv_rect(4, 194, 232, 1, K_EDGE);
    t[0] = 'T';
    t[1] = (char)('1' + sq.trk);
    t[2] = 0;
    cv_text(8, 196, &FONT_B, t, K_RED);
    cv_text(28, 196, &FONT_B, writing ? "STEP REC" : ui.rec ? "REC" : sq.t[sq.trk].mute ? "MUTED" : "SEQ",
            sq.t[sq.trk].mute && !ui.rec ? K_GREY : K_RED);
    page = writing ? ui.cursor / 16 : st >= 0 ? st / 16 : 0;
    t[0] = 0;
    int_text(page * 16 + 1, t);
    text_cat(t, "-");
    int_text(page * 16 + 16, t + text_len_(t));
    text_cat(t, "/");
    int_text(len, t + text_len_(t));
    cv_text(writing ? 96 : 76, 199, &FONT_XS, t, K_CREAM);
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
    cv_text(194, 199, &FONT_XS, eng_arp_on() ? "ARP" : RATE_NAME[sq.t[sq.trk].rate], eng_arp_on() ? K_RED : K_GREY);
    for (i = 0; i < 16; i++) {
        int s = page * 16 + i, n = 0, k, tie;
        int32_t x = 6 + i * 14, y = 211;
        const step_t *sp = &sq.s.st[sq.trk][s];
        tie = sp->note[0] == SN_TIE;
        for (k = 0; k < SEQ_POLY; k++)
            if (sp->note[k] >= 12)
                n++;
        cv_rect(x, y, 13, 18, s < len ? K_CELL : K_PANEL);
        cv_rect(x, y, 13, 3, s < len ? RGB(66, 66, 70) : K_PANEL);
        if (n) {                         /* its notes: as loud as tall, as long as wide; a chord: a mark on top */
            int32_t hgt = 3 + step_vel(sp) * 15 / 127, wid = 2 + step_gate(sp) * 11 / 100;
            cv_rect(x, y + 18 - hgt, wid, hgt, s < len ? K_RED : K_RED_M);
            if (n > 1)
                cv_rect(x + 4, y + 18 - hgt - 3, 5, 2, K_CREAM);
        }
        if (tie)
            cv_rect(x - 1, y + 10, 15, 4, K_RED_M);
        if (s == st)
            frame_rect(x - 1, y - 1, 15, 20, K_CREAM);
        if (writing && s == ui.cursor && (frame_n & 8u))
            frame_rect(x - 1, y - 1, 15, 20, K_CREAM);
        if ((i & 3) == 0) {
            int_text(s + 1, t);
            cv_text(x + (s < 9 ? 3 : 0), 230, &FONT_XS, t, i == 0 ? K_CREAM : K_GREY);
        }
    }
    if (st >= 0 && st / 16 == page)      /* the playhead */
        cv_rect(6 + (st & 15) * 14 + 6, 209, 1, 22, K_CREAM);
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
        ctext(120, 70, &FONT_L, "PROBLEM 6", K_CREAM);
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
    song_apply();
    sound_globals();
    for (k = 0; k < NKEYS; k++)
        key_sounding[k] = -1;
    for (k = 0; k < 128; k++)
        live_st[k] = -1;
    ui.copy_to = -1;
    for (k = 0; k < NPARAM; k++)
        cc_new[k] = 255u;
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
    touch_on(1);
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
        char t[64] = "PROBLEM 6. ";
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
    /* Writing the flash stops the sound for a moment (an erase: 8-17 ms with the audio held). So nothing is written
     * while anything sounds: the settings and the sequence wait for a silence (no voice, no reverb tail, no key, the
     * sequencer stopped) of a second and a half. Only SAVE writes at once, the sound faded out and in around it. */
    if (eng_voices() > 0 || eng_tail() > 0.002f || eng_playing() || ui.keys || ui.rec_held)
        silent_since = now;
    if (save_now) {
        eng_duck(1);
        if (eng_ducked() || now - save_at > 60u) {
            settings_save();
            eng_duck(0);
            save_now = 0;
        }
    } else {
        save_at = now;
    }
    if (now - silent_since > 1500u) {
        if (set_dirty && now - dirty_at > 2000u)
            settings_save();
        else if (seq_dirty && now - seq_dirty_at > 2000u)
            seq_save();
    }
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
    song_apply();
    sound_globals();
    ui.redraw = 1;
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "page"))
        *got = ui.page;
    else if (!strncmp(what, "p", 1) && what[1] >= '0' && what[1] <= '9')   /* p12: the sound's parameter 12 */
    {
        int n = 0, i;
        for (i = 1; what[i] >= '0' && what[i] <= '9'; i++)
            n = n * 10 + (what[i] - '0');
        *got = n < NPARAM ? ed.p[n] : -1;
    }
    else if (!strcmp(what, "cur"))
        *got = CUR;
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
        *got = eng_seq_step(sq.trk);
    else if (!strcmp(what, "trk"))
        *got = sq.trk;
    else if (!strcmp(what, "len"))
        *got = seq_len();
    else if (!strcmp(what, "click"))
        *got = set.click;
    else if (!strcmp(what, "ties") || !strcmp(what, "gmin")) {   /* the selected track: its TIEs; its shortest gate */
        int i, c = 0, gm = 101;
        for (i = 0; i < seq_len(); i++) {
            const step_t *sp = step_at(i);
            if (sp->note[0] == SN_TIE)
                c++;
            else if (step_low(sp) && sp->gate < gm)
                gm = sp->gate;
        }
        *got = what[0] == 't' ? c : gm;
    }
    else if (!strncmp(what, "vt", 2)) {  /* vtN: the voices sounding track N's sound */
        int v, c = 0;
        for (v = 0; v < NVOICE; v++)
            if (eng_voice_track(v) == atoi(what + 2))
                c++;
        *got = c;
    } else if (!strncmp(what, "rv", 2))  /* rvN: track N's REVERB MIX (the effects: the song's) */
        *got = sq.t[atoi(what + 2) & 3].snd.p[P_RV_MIX];
    else if (!strncmp(what, "mute", 4))
        *got = sq.t[atoi(what + 4) & 3].mute;
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
        *got = s >= 0 && s < TRK_STEPS ? sq.s.st[sq.trk][s].note[0] : -1;
    } else if (!strncmp(what, "v", 1) && what[1] >= '0' && what[1] <= '9') { /* vN: step N's velocity */
        int s = atoi(what + 1);
        *got = s >= 0 && s < TRK_STEPS ? sq.s.st[sq.trk][s].vel : -1;
    } else if (!strncmp(what, "g", 1) && what[1] >= '0' && what[1] <= '9') { /* gN: step N's gate */
        int s = atoi(what + 1);
        *got = s >= 0 && s < TRK_STEPS ? sq.s.st[sq.trk][s].gate : -1;
    } else if (!strncmp(what, "n", 1) && what[1] >= '0' && what[1] <= '9') { /* nN: step N's notes */
        int s = atoi(what + 1), k, c = 0;
        for (k = 0; k < SEQ_POLY && s >= 0 && s < TRK_STEPS; k++)
            if (sq.s.st[sq.trk][s].note[k] >= 12)
                c++;
        *got = c;
    } else if (what[0] == 'k' && what[1] >= '1' && what[1] <= '4' && !what[2])
        *got = knob_get(PAGES[ui.page].k[what[1] - '1']);
    else if (!strncmp(what, "ks", 2))    /* ksN: the note key N plays (-1 none) */
        *got = key_sounding[atoi(what + 2) % NKEYS];
    else if (!strcmp(what, "root"))
        *got = root;
    else if (!strcmp(what, "split"))
        *got = set.split_oct;
    else if (!strcmp(what, "press")) {   /* the most pressed voice, x1000 */
        int v;
        float m = 0.0f;
        for (v = 0; v < NVOICE; v++)
            m = fm_maxf(m, eng_voice_press(v));
        *got = (int)(m * 1000.0f);
    } else if (!strcmp(what, "level"))     /* the output, x1000 */
        *got = (int)(eng_level() * 1000.0f);
    else if (!strcmp(what, "tail"))
        *got = (int)(eng_tail() * 1000.0f);
    else
        return 0;
    return 1;
}
#endif
