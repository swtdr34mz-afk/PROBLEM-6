/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (PROPHET 67) */
/* PROPHET 67 by swtdr34mz: a 7-voice analogue-style polysynth for the M-VAVE FM-1 (on fm1-base), in the manner of the
 * Sequential Prophet-6. This header is the app's half of the contract with the platform (device/unity.c,
 * host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "PROPHET 67"
#define APP_SPLASH_RGB RGB(232, 40, 32)
#define APP_SPLASH                       /* app_splash: the panel's own title, not the platform's page */
#define APP_TICK                         /* app_tick: MIDI in (notes, controllers, clock), at 1 kHz */
void app_tick(void);
#define APP_OVERLOAD                     /* app_overload: the render ran late: the quietest voice let go */
void app_overload(void);
#define APP_BLOCK 128u                   /* frames per audio block: 2.9 ms at 44.1 kHz (a keyboard: low latency) */
#define APP_TOUCH                        /* velocity and pressure from the keys (lib/touch.h): AFTERTOUCH */
#define APP_CANVAS_PX (240u * 120u)      /* the screen drawn in two bands of 120 rows (the reverbs need the pool) */

/* the store: the settings (presets, the globals), the panel calibration (the platform's), the sequence */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_SEQ, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0xDE000u}

/* backup and restore over USB: the presets, the globals and the sequence */
#define APP_BACKUP_OBJS {OBJ_SETTINGS, OBJ_SEQ}
#define APP_BACKUP_MAGIC 0x37364850u            /* "PH67" */
#define APP_BACKUP_VERSION 1u
#define APP_BACKUP_WHAT "replaces presets and sequence"
int app_backup_playing(void);
void app_backup_flush(void);
void app_backup_hold(int on);
void app_backup_restored(void);

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
void app_splash(void);

#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
