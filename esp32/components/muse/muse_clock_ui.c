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

#define HAND_PERIOD_MS 200 /* smooth hour/minute updates while swiping */
#define DATE_BUF_LEN 32
#define SECOND_HEAD_LEN 24
#define SECOND_HEAD_WIDTH 4
#define SECOND_HEAD_PAD 22

/* Neon palette (RGB888), blended around the bezel and reused by the specks. */
#define COL_CYAN 0x3FE0FF
#define COL_BLUE 0x2A7BFF
#define COL_MAGENTA 0xFF2D95
#define COL_ORANGE 0xFF8A2B
#define COL_GOLD 0xFFC84D
#define COL_PALE_CYAN 0x8FD6FF
#define COL_HAND 0xFF1E8C      /* hot-pink hand halo */
#define COL_HAND_CORE 0xFFC8E6 /* pale-pink core */
#define COL_MARKER 0x46E8FF    /* cyan cardinal markers */

static lv_obj_t *s_tile;
static lv_obj_t *s_bg;       /* static artwork overlay */
static lv_obj_t *s_bg_img;   /* sky background image */
static lv_obj_t *s_sec_head; /* moving second "head" bar on the rim */
static lv_obj_t *s_date_lbl;
static lv_obj_t *s_pivot;   /* centre cap, above the hands */
static lv_obj_t *s_hour[3]; /* [0] soft halo, [1] halo, [2] pale core */
static lv_obj_t *s_min[3];
static lv_obj_t *s_markers[4]; /* cyan capsule markers at 12/3/6/9 */

static lv_point_precise_t s_hour_pts[2];
static lv_point_precise_t s_min_pts[2];
static lv_point_precise_t s_marker_pts[4][2];
static lv_point_precise_t s_sec_pts[2];

static lv_timer_t *s_hand_timer;
static bool s_running;

/* Fixed tile-relative geometry, computed once at build from the display size.
 * The static-artwork draw uses the widget's live position instead, so this is
 * only the resting centre the hands, markers and fireflies are laid out around. */
static int s_cx, s_cy, s_radius;

/* ---- colour helpers ---------------------------------------------------- */

static lv_color_t rgb(uint32_t c)
{
    return lv_color_hex(c);
}

static uint32_t lerp_rgb(uint32_t a, uint32_t b, float t)
{
    int ar = (a >> 16) & 0xff, ag = (a >> 8) & 0xff, ab = a & 0xff;
    int br = (b >> 16) & 0xff, bg = (b >> 8) & 0xff, bb = b & 0xff;
    int r = (int)(ar + (br - ar) * t + 0.5f);
    int g = (int)(ag + (bg - ag) * t + 0.5f);
    int bl = (int)(ab + (bb - ab) * t + 0.5f);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

/*
 * The bezel's colour by LVGL angle (0 deg = 3 o'clock, 90 = 6 o'clock). Upper
 * and left read gold/orange, the right and lower-right electric blue and cyan,
 * with magenta near the bottom. Eight keyframes, linearly blended.
 */
static uint32_t bezel_color(float deg)
{
    static const uint32_t key[8] = {
        COL_CYAN,     /*   0 - 3 o'clock */
        COL_BLUE,     /*  45 */
        COL_MAGENTA,  /*  90 - 6 o'clock */
        COL_ORANGE,   /* 135 */
        COL_GOLD,     /* 180 - 9 o'clock */
        COL_GOLD,     /* 225 */
        0xFFB13D,     /* 270 - 12 o'clock, gold-orange */
        COL_PALE_CYAN /* 315 */
    };
    while (deg < 0)
        deg += 360;
    while (deg >= 360)
        deg -= 360;
    float seg = deg / 45.0f;
    int i = (int)seg;
    float t = seg - i;
    return lerp_rgb(key[i & 7], key[(i + 1) & 7], t);
}

/* ---- static artwork ---------------------------------------------------- */

/* A short radial tick from radius r1 to r2 at LVGL angle `deg`. */
static void draw_tick(lv_layer_t *layer, int cx, int cy, float deg, int r1, int r2, int width,
                      uint32_t color, lv_opa_t opa)
{
    float a = deg * (float)M_PI / 180.0f;
    float ca = cosf(a), sa = sinf(a);
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = rgb(color);
    d.width = width;
    d.opa = opa;
    d.round_start = d.round_end = 1;
    d.p1.x = cx + ca * r1;
    d.p1.y = cy + sa * r1;
    d.p2.x = cx + ca * r2;
    d.p2.y = cy + sa * r2;
    lv_draw_line(layer, &d);
}

static void draw_speck(lv_layer_t *layer, int x, int y, int r, uint32_t color, lv_opa_t opa)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = LV_RADIUS_CIRCLE;
    d.bg_color = rgb(color);
    d.bg_opa = opa;
    lv_area_t a = {x - r, y - r, x + r, y + r};
    lv_draw_rect(layer, &d, &a);
}

/* The layered glowing bezel, tick marks and sparse specks. Drawn into whatever
 * region LVGL asks to redraw. The centre is taken from the widget's live
 * position so the artwork tracks the tile as it slides. */
static void on_bg_draw(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t c;
    lv_obj_get_coords(s_bg, &c);
    const int cx = (c.x1 + c.x2) / 2;
    const int cy = (c.y1 + c.y2) / 2;

    /* Concentric bezel outlines: a wide dim ring for the glow and two thin
     * bright ones over it, each split into short arcs so the colour can sweep
     * from gold to cyan around the dial. */
    const int R = s_radius;
    const struct
    {
        int radius, width;
        lv_opa_t opa;
    } rings[3] = {
        {R, 9, LV_OPA_40},        /* soft outer halo */
        {R - 4, 3, LV_OPA_COVER}, /* bright edge */
        {R - 13, 2, LV_OPA_70},   /* inner outline */
    };
    for (int r = 0; r < 3; r++)
    {
        lv_draw_arc_dsc_t d;
        lv_draw_arc_dsc_init(&d);
        d.center.x = cx;
        d.center.y = cy;
        d.radius = rings[r].radius;
        d.width = rings[r].width;
        d.opa = rings[r].opa;
        d.rounded = 0;
        for (int seg = 0; seg < 48; seg++)
        {
            float a0 = seg * 7.5f;
            d.start_angle = a0;
            d.end_angle = a0 + 7.7f; /* slight overlap hides seams */
            d.color = rgb(bezel_color(a0 + 3.75f));
            lv_draw_arc(layer, &d);
        }
    }

    /* 60 fine ticks just inside the bezel: mostly cyan, a warm highlight every
     * quarter, and a longer mark at each of the twelve hours. */
    for (int i = 0; i < 60; i++)
    {
        float deg = i * 6.0f;
        bool hour = (i % 5) == 0;
        bool warm = (i % 15) == 0;
        int len = hour ? 11 : 6;
        int r2 = R - 18;
        int r1 = r2 - len;
        draw_tick(layer, cx, cy, deg, r1, r2, hour ? 3 : 2,
                  warm ? COL_GOLD : COL_CYAN, hour ? LV_OPA_COVER : LV_OPA_70);
    }

    /* Sparse specks within and around the face. Fixed positions so they never
     * shimmer with the hands. */
    static const struct
    {
        int da, dr, r;
        uint32_t color;
    } specks[12] = {
        {20, 70, 2, COL_GOLD},
        {75, 120, 1, COL_CYAN},
        {130, 60, 2, COL_MAGENTA},
        {160, 150, 1, COL_GOLD},
        {205, 90, 1, COL_CYAN},
        {250, 130, 2, COL_BLUE},
        {290, 55, 1, COL_GOLD},
        {315, 110, 2, COL_CYAN},
        {40, 150, 1, COL_PALE_CYAN},
        {100, 160, 1, COL_BLUE},
        {225, 160, 2, COL_GOLD},
        {340, 140, 1, COL_MAGENTA},
    };
    for (int i = 0; i < 12; i++)
    {
        float a = specks[i].da * (float)M_PI / 180.0f;
        int x = cx + (int)(cosf(a) * specks[i].dr);
        int y = cy + (int)(sinf(a) * specks[i].dr);
        draw_speck(layer, x, y, specks[i].r, specks[i].color, LV_OPA_80);
    }
}

/* ---- assets ------------------------------------------------------------ */

static void setup_background_image(int w, int h)
{
    static const char *const candidates[] = {
        "A:/clock_sky.png",
        "S:/clock_sky.png",
        "/muse/clock_sky.png",
        "clock_sky.png",
    };

    s_bg_img = lv_image_create(s_tile);
    lv_obj_remove_style_all(s_bg_img);
    lv_obj_set_size(s_bg_img, w, h);
    lv_obj_set_pos(s_bg_img, 0, 0);
    lv_obj_remove_flag(s_bg_img, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    bool loaded = false;
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++)
    {
        FILE *f = fopen(candidates[i], "rb");
        if (!f)
        {
            continue;
        }
        fclose(f);
        lv_image_set_src(s_bg_img, candidates[i]);
        loaded = true;
        break;
    }

    if (!loaded)
    {
        lv_obj_set_style_bg_color(s_bg_img, lv_color_hex(0x0A1630), 0);
        lv_obj_set_style_bg_opa(s_bg_img, LV_OPA_COVER, 0);
    }
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

    char date_buf[DATE_BUF_LEN];
    if (synced)
    {
        strftime(date_buf, sizeof(date_buf), "%d-%b-%Y", &tmv);
    }
    else
    {
        strlcpy(date_buf, "-- --- ----", sizeof(date_buf));
    }

    char rainbow[96];
    snprintf(rainbow, sizeof(rainbow),
             "#ff3b30%.2s##ff9500-##ffcc00%.3s##34c759-##0a84ff%.4s#",
             date_buf, date_buf + 3, date_buf + 7);
    if (strcmp(rainbow, lv_label_get_text(s_date_lbl)) != 0)
    {
        lv_label_set_text(s_date_lbl, rainbow);
    }

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
    s_tile = s_bg = s_bg_img = s_sec_head = s_date_lbl = s_pivot = NULL;
    for (int i = 0; i < 3; i++)
        s_hour[i] = s_min[i] = NULL;
    for (int i = 0; i < 4; i++)
        s_markers[i] = NULL;
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

    /* Overlay artwork layer (bezel, ticks and specks) above the sky image. */
    s_bg = lv_obj_create(tile);
    lv_obj_remove_style_all(s_bg);
    lv_obj_set_size(s_bg, w, h);
    lv_obj_set_pos(s_bg, 0, 0);
    lv_obj_remove_flag(s_bg, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_bg, on_bg_draw, LV_EVENT_DRAW_MAIN, NULL);

    /* Four cyan capsule markers at the cardinal positions. */
    for (int i = 0; i < 4; i++)
    {
        float deg = i * 90.0f;
        lv_obj_t *m = lv_line_create(tile);
        lv_obj_remove_style_all(m);
        lv_obj_set_style_line_width(m, 9, 0);
        lv_obj_set_style_line_color(m, rgb(COL_MARKER), 0);
        lv_obj_set_style_line_rounded(m, true, 0);
        lv_obj_remove_flag(m, LV_OBJ_FLAG_CLICKABLE);
        float a = deg * (float)M_PI / 180.0f;
        int r2 = s_radius - 20;
        int r1 = r2 - 18;
        s_marker_pts[i][0].x = s_cx + cosf(a) * r1;
        s_marker_pts[i][0].y = s_cy + sinf(a) * r1;
        s_marker_pts[i][1].x = s_cx + cosf(a) * r2;
        s_marker_pts[i][1].y = s_cy + sinf(a) * r2;
        lv_line_set_points(m, s_marker_pts[i], 2);
        s_markers[i] = m;
    }

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

    /* Bottom date label: small font with inline rainbow recolor. */
    s_date_lbl = lv_label_create(tile);
    lv_obj_remove_style_all(s_date_lbl);
#if LV_FONT_MONTSERRAT_12
    lv_obj_set_style_text_font(s_date_lbl, &lv_font_montserrat_12, 0);
#else
    lv_obj_set_style_text_font(s_date_lbl, &lv_font_montserrat_14, 0);
#endif
    lv_obj_set_style_text_align(s_date_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_date_lbl, w - 24);
    lv_obj_align(s_date_lbl, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_label_set_recolor(s_date_lbl, true);

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
