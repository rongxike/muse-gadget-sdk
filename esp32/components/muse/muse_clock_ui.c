/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* localtime_r/setenv/tzset need the POSIX/default declarations under a strict
 * host libc; harmless on newlib (ESP-IDF). Must precede all includes. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include "muse_clock_ui.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
 * Timezone: reuse the app's existing clock timezone when the idle-screen clock
 * is configured; otherwise fall back to the same default. setenv() here never
 * overwrites a value the idle clock already set (last arg 0).
 */
#ifdef CONFIG_MUSE_CLOCK_TZ
#define CLOCK_TZ CONFIG_MUSE_CLOCK_TZ
#else
#define CLOCK_TZ "CST-8"
#endif

#define HAND_PERIOD_MS 1000 /* the hands step once a second; no need to run faster */
#define SECOND_HEAD_LEN 24
#define SECOND_HEAD_WIDTH 4
#define SECOND_HEAD_PAD 22

/* Hand palette (RGB888). The rest of the face is baked into clock_face.c. */
#define COL_MAGENTA 0xFF2D95
#define COL_HAND 0xFF1E8C      /* hot-pink hand halo */
#define COL_HAND_CORE 0xFFC8E6 /* pale-pink core */

static lv_obj_t *s_tile;
static lv_obj_t *s_bg_img;   /* baked clock-face background image */
static lv_obj_t *s_sec_head; /* moving second "head" bar on the rim */
static lv_obj_t *s_pivot;   /* centre cap, above the hands */
static lv_obj_t *s_hour[3]; /* [0] soft halo, [1] halo, [2] pale core */
static lv_obj_t *s_min[3];

static lv_point_precise_t s_hour_pts[2];
static lv_point_precise_t s_min_pts[2];
static lv_point_precise_t s_sec_pts[2];

static lv_timer_t *s_hand_timer;
static bool s_running;

/* Fixed tile-relative geometry, computed once at build from the display size,
 * for laying out the moving hands over the baked face. */
static int s_cx, s_cy, s_radius;

/* ---- colour helpers ---------------------------------------------------- */

static lv_color_t rgb(uint32_t c)
{
    return lv_color_hex(c);
}

/* ---- baked clock face -------------------------------------------------- */

/*
 * The entire static face (sky, bezel, tick marks, specks, Roman numerals and
 * cardinal markers) is pre-rendered into one RGB565_SWAPPED image baked into
 * flash (clock_face.c). The display runs in RGB565_SWAPPED (muse_lcd_bands.c),
 * so LVGL blits it straight from flash with no decode, no PSRAM buffer, and no
 * per-frame vector drawing. Only the hands and second bar move.
 */
extern const lv_image_dsc_t clock_face;

/* ---- assets ------------------------------------------------------------ */

static void setup_background_image(int w, int h)
{
    s_bg_img = lv_image_create(s_tile);
    lv_obj_remove_style_all(s_bg_img);
    lv_obj_set_size(s_bg_img, w, h);
    lv_obj_set_pos(s_bg_img, 0, 0);
    lv_obj_remove_flag(s_bg_img, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_image_set_src(s_bg_img, &clock_face);
}

/* ---- hands ------------------------------------------------------------- */

/* Point a two-point line object along `deg` (0 = straight up / 12 o'clock),
 * from `tail` behind the centre to `len` ahead of it. */
static void set_hand(lv_obj_t *line, lv_point_precise_t pts[2], float deg, int len, int tail)
{
    float a = (deg - 90.0f) * (float)M_PI / 180.0f; /* 0 deg -> 12 o'clock */
    float ca = cosf(a), sa = sinf(a);
    pts[0].x = s_cx - ca * tail;
    pts[0].y = s_cy - sa * tail;
    pts[1].x = s_cx + ca * len;
    pts[1].y = s_cy + sa * len;
    lv_line_set_points(line, pts, 2);
}

static void update_hands(void)
{
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    bool synced = (tmv.tm_year + 1900) >= 2021;

    float hour_deg, min_deg, sec_deg;
    if (synced)
    {
        float mins = tmv.tm_min + tmv.tm_sec / 60.0f;
        float hours = (tmv.tm_hour % 12) + mins / 60.0f;
        hour_deg = hours * 30.0f;
        min_deg = mins * 6.0f;
        sec_deg = tmv.tm_sec * 6.0f;
    }
    else
    {
        /* Time not set: park indicators straight up rather than invent one. */
        hour_deg = 0;
        min_deg = 0;
        sec_deg = 0;
    }

    int hlen = (int)(s_radius * 0.52f);
    int mlen = (int)(s_radius * 0.76f);
    int tail = (int)(s_radius * 0.12f);
    for (int i = 0; i < 3; i++)
    {
        set_hand(s_hour[i], s_hour_pts, hour_deg, hlen, tail);
        set_hand(s_min[i], s_min_pts, min_deg, mlen, tail);
    }

    float a = (sec_deg - 90.0f) * (float)M_PI / 180.0f;
    float ca = cosf(a), sa = sinf(a);
    int r2 = s_radius - SECOND_HEAD_PAD;
    int r1 = r2 - SECOND_HEAD_LEN;
    s_sec_pts[0].x = s_cx + ca * r1;
    s_sec_pts[0].y = s_cy + sa * r1;
    s_sec_pts[1].x = s_cx + ca * r2;
    s_sec_pts[1].y = s_cy + sa * r2;
    lv_line_set_points(s_sec_head, s_sec_pts, 2);

    /* Dim the unsynced placeholder so it clearly reads as "not set yet". */
    lv_opa_t soft = synced ? LV_OPA_30 : LV_OPA_10;
    lv_opa_t halo = synced ? LV_OPA_COVER : LV_OPA_40;
    lv_obj_set_style_line_opa(s_hour[0], soft, 0);
    lv_obj_set_style_line_opa(s_min[0], soft, 0);
    lv_obj_set_style_line_opa(s_hour[1], halo, 0);
    lv_obj_set_style_line_opa(s_min[1], halo, 0);
    lv_obj_set_style_line_opa(s_hour[2], halo, 0);
    lv_obj_set_style_line_opa(s_min[2], halo, 0);
    lv_obj_set_style_line_opa(s_sec_head, synced ? LV_OPA_COVER : LV_OPA_40, 0);
}

static void hand_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    update_hands();
}

/* ---- build / lifecycle ------------------------------------------------- */

static lv_obj_t *make_hand(lv_obj_t *parent, int width, uint32_t color)
{
    lv_obj_t *l = lv_line_create(parent);
    lv_obj_remove_style_all(l);
    lv_obj_set_style_line_width(l, width, 0);
    lv_obj_set_style_line_color(l, rgb(color), 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

static void on_tile_delete(lv_event_t *e)
{
    (void)e;
    if (s_hand_timer)
    {
        lv_timer_del(s_hand_timer);
        s_hand_timer = NULL;
    }
    s_running = false;
    s_tile = s_bg_img = s_sec_head = s_pivot = NULL;
    for (int i = 0; i < 3; i++)
        s_hour[i] = s_min[i] = NULL;
}

void muse_clock_ui_build(lv_obj_t *tile)
{
    s_tile = tile;
    setenv("TZ", CLOCK_TZ, 0); /* 0: don't clobber an already-set TZ */
    tzset();

    int w = lv_display_get_horizontal_resolution(NULL);
    int h = lv_display_get_vertical_resolution(NULL);
    s_cx = w / 2;
    s_cy = h / 2;
    s_radius = (w < h ? w : h) / 2 - 6;

    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(tile, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);

    setup_background_image(w, h);

    /* Hour/minute hands. */
    s_hour[0] = make_hand(tile, 18, COL_MAGENTA);
    s_hour[1] = make_hand(tile, 10, COL_HAND);
    s_hour[2] = make_hand(tile, 4, COL_HAND_CORE);
    s_min[0] = make_hand(tile, 16, COL_MAGENTA);
    s_min[1] = make_hand(tile, 8, COL_HAND);
    s_min[2] = make_hand(tile, 3, COL_HAND_CORE);

    /* Second indicator: only a short white head near the rim. */
    s_sec_head = lv_line_create(tile);
    lv_obj_remove_style_all(s_sec_head);
    lv_obj_set_style_line_width(s_sec_head, SECOND_HEAD_WIDTH, 0);
    lv_obj_set_style_line_color(s_sec_head, lv_color_white(), 0);
    lv_obj_set_style_line_rounded(s_sec_head, true, 0);
    lv_obj_remove_flag(s_sec_head, LV_OBJ_FLAG_CLICKABLE);

    /* Centre pivot, above the hands. */
    s_pivot = lv_obj_create(tile);
    lv_obj_remove_style_all(s_pivot);
    lv_obj_set_size(s_pivot, 12, 12);
    lv_obj_set_style_radius(s_pivot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_pivot, rgb(COL_HAND_CORE), 0);
    lv_obj_set_style_bg_opa(s_pivot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_pivot, 2, 0);
    lv_obj_set_style_border_color(s_pivot, rgb(COL_HAND), 0);
    lv_obj_set_style_shadow_width(s_pivot, 8, 0);
    lv_obj_set_style_shadow_color(s_pivot, rgb(COL_MAGENTA), 0);
    lv_obj_remove_flag(s_pivot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_pivot, LV_ALIGN_CENTER, 0, 0);

    update_hands();

    /* Free page-owned timers if the tile (and so the whole page) is destroyed. */
    lv_obj_add_event_cb(tile, on_tile_delete, LV_EVENT_DELETE, NULL);
}

void muse_clock_ui_tick(bool visible)
{
    if (!s_tile)
    {
        return;
    }
    if (visible && !s_running)
    {
        /* Becoming visible: show the right time at once, then run one timer. */
        update_hands();
        if (!s_hand_timer)
        {
            s_hand_timer = lv_timer_create(hand_timer_cb, HAND_PERIOD_MS, NULL);
        }
        s_running = true;
    }
    else if (!visible && s_running)
    {
        /* Hidden: stop all page-specific animation work. */
        if (s_hand_timer)
        {
            lv_timer_del(s_hand_timer);
            s_hand_timer = NULL;
        }
        s_running = false;
    }
}
