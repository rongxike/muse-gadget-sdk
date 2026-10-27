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

#include "muse_flappy_ui.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_random.h"
#include "nvs.h"

#include "muse_audio.h"

/* ---- tuning ------------------------------------------------------------ */

#define FRAME_MS 30             /* game loop period (~33 fps) */
#define BIRD_FRAME_MS 120       /* wing-flap frame advance */

#define GRAVITY 1500.0f         /* px / s^2 */
#define JUMP_V (-430.0f)        /* px / s imparted by a flap */
#define MAX_FALL 650.0f         /* clamp downward speed for control */

#define BIRD_SIZE 48            /* one animation frame is 48x48 */
#define BIRD_HALF 17            /* collision half-box (a touch forgiving) */

#define PIPE_W 72               /* pipe image width */
#define PIPE_IMG_H 370          /* pipe image height */
#define PIPE_GAP 162            /* vertical opening between the pipes */
#define PIPE_EDGE 44            /* keep the gap this far from the frame */
#define PIPE_SPEED 150.0f       /* px / s the pipes travel left */
#define PIPE_SPACING 232        /* horizontal distance between pipe pairs */
#define NUM_PIPES 4             /* recycled pool; covers the screen width */

#define NVS_NS "flappy"
#define NVS_KEY_HI "hi"

/* ---- state ------------------------------------------------------------- */

typedef enum
{
    ST_READY = 0, /* waiting for the first tap */
    ST_PLAY,
    ST_OVER,
} game_state_t;

/* Baked art (flappy_*.c), blitted straight from flash. */
extern const lv_image_dsc_t flappy_bezel;
extern const lv_image_dsc_t flappy_bird_0;
extern const lv_image_dsc_t flappy_bird_1;
extern const lv_image_dsc_t flappy_bird_2;
extern const lv_image_dsc_t flappy_pipe_top;
extern const lv_image_dsc_t flappy_pipe_bottom;
extern const lv_image_dsc_t flappy_getready;
extern const lv_image_dsc_t flappy_gameover;

static const lv_image_dsc_t *const s_bird_frames[3] = {
    &flappy_bird_0,
    &flappy_bird_1,
    &flappy_bird_2,
};

/* ---- sound effects ----------------------------------------------------- */

/* 16 kHz mono signed-16 PCM pre-decoded from the mp3s (flappy_sfx.c). */
extern const int16_t flappy_sfx_flap[];
extern const size_t flappy_sfx_flap_len;
extern const int16_t flappy_sfx_over[];
extern const size_t flappy_sfx_over_len;
extern const int16_t flappy_sfx_ready[];
extern const size_t flappy_sfx_ready_len;

typedef struct
{
    const int16_t *pcm;
    size_t len;
} sfx_req_t;

static QueueHandle_t s_sfx_q;
static TaskHandle_t s_sfx_task;

/* Dedicated task so a clip never blocks the LVGL/game loop. A newer request
 * interrupts the one playing, which keeps flaps snappy over the long clips. */
static void sfx_task(void *arg)
{
    (void)arg;
    sfx_req_t req;
    for (;;)
    {
        if (xQueueReceive(s_sfx_q, &req, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }
        muse_audio_power(true); /* idempotent; make sure the codec is open */
        size_t off = 0;
        while (off < req.len)
        {
            sfx_req_t peek;
            if (xQueuePeek(s_sfx_q, &peek, 0) == pdTRUE)
            {
                break; /* a newer sound is waiting: switch to it */
            }
            size_t n = req.len - off;
            if (n > MUSE_AUDIO_CHUNK)
            {
                n = MUSE_AUDIO_CHUNK;
            }
            muse_audio_write(req.pcm + off, n);
            off += n;
        }
    }
}

static void sfx_play(const int16_t *pcm, size_t len)
{
    if (!s_sfx_q || !pcm || len == 0)
    {
        return;
    }
    sfx_req_t req = {pcm, len};
    xQueueSend(s_sfx_q, &req, 0); /* never block the caller */
}

typedef struct
{
    lv_obj_t *top;
    lv_obj_t *bottom;
    float x;      /* left edge, tile coordinates */
    int gap_top;  /* y of the top pipe's lower edge */
    bool scored;
} pipe_t;

static lv_obj_t *s_tile;
static lv_obj_t *s_bird;
static lv_obj_t *s_score_lbl;
static lv_obj_t *s_best_lbl;
static lv_obj_t *s_ready_img;
static lv_obj_t *s_over_img;
static lv_obj_t *s_bezel_img;
static pipe_t s_pipes[NUM_PIPES];

static lv_timer_t *s_timer;
static bool s_running;

static game_state_t s_state;
static float s_bird_y;   /* bird centre y */
static float s_bird_vel; /* px / s */
static int s_bird_x;     /* fixed column (centre x) */
static int s_frame;      /* current wing frame 0..2 */
static uint32_t s_frame_ms_acc;
static int s_score;
static int s_high;

static int s_w, s_h;

/* ---- high score (NVS) -------------------------------------------------- */

static void load_high(void)
{
    s_high = 0;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK)
    {
        int32_t v = 0;
        if (nvs_get_i32(h, NVS_KEY_HI, &v) == ESP_OK)
        {
            s_high = (int)v;
        }
        nvs_close(h);
    }
}

static void save_high(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK)
    {
        nvs_set_i32(h, NVS_KEY_HI, (int32_t)s_high);
        nvs_commit(h);
        nvs_close(h);
    }
}

/* ---- helpers ----------------------------------------------------------- */

static void set_score_text(void)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", s_score);
    lv_label_set_text(s_score_lbl, buf);
}

static void place_pipe(pipe_t *p)
{
    /* Top image hangs so its lower edge sits at gap_top; bottom image starts
     * at gap_top + gap. Parts that run off the frame are simply clipped. */
    lv_obj_set_pos(p->top, (int)p->x, p->gap_top - PIPE_IMG_H);
    lv_obj_set_pos(p->bottom, (int)p->x, p->gap_top + PIPE_GAP);
}

static int random_gap_top(void)
{
    int span = s_h - PIPE_GAP - 2 * PIPE_EDGE;
    if (span < 1)
    {
        span = 1;
    }
    return PIPE_EDGE + (int)(esp_random() % (uint32_t)span);
}

static void show_pipes(bool show)
{
    for (int i = 0; i < NUM_PIPES; i++)
    {
        if (show)
        {
            lv_obj_clear_flag(s_pipes[i].top, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_pipes[i].bottom, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(s_pipes[i].top, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_pipes[i].bottom, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* Lay the pool out to the right of the screen, each one PIPE_SPACING apart. */
static void reset_pipes(void)
{
    for (int i = 0; i < NUM_PIPES; i++)
    {
        s_pipes[i].x = s_w + 60 + i * PIPE_SPACING;
        s_pipes[i].gap_top = random_gap_top();
        s_pipes[i].scored = false;
        place_pipe(&s_pipes[i]);
    }
}

static void set_bird_frame(int frame)
{
    s_frame = frame;
    lv_image_set_src(s_bird, s_bird_frames[frame]);
}

static void position_bird(void)
{
    lv_obj_set_pos(s_bird, s_bird_x - BIRD_SIZE / 2, (int)s_bird_y - BIRD_SIZE / 2);
}

static void enter_ready(void)
{
    s_state = ST_READY;
    s_score = 0;
    s_bird_vel = 0;
    s_bird_y = s_h * 0.42f;
    set_bird_frame(1);
    position_bird();
    set_score_text();
    show_pipes(false);
    reset_pipes();
    lv_obj_add_flag(s_over_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_best_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_ready_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_score_lbl, LV_OBJ_FLAG_HIDDEN);
}

static void enter_play(void)
{
    s_state = ST_PLAY;
    lv_obj_add_flag(s_ready_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_over_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_best_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_score_lbl, LV_OBJ_FLAG_HIDDEN);
    show_pipes(true);
    s_bird_vel = JUMP_V; /* a flap to start, like the original */
}

static void enter_over(void)
{
    s_state = ST_OVER;
    if (s_score > s_high)
    {
        s_high = s_score;
        save_high();
    }
    char buf[24];
    snprintf(buf, sizeof(buf), "BEST  %d", s_high);
    lv_label_set_text(s_best_lbl, buf);
    lv_obj_clear_flag(s_best_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_over_img, LV_OBJ_FLAG_HIDDEN);
    sfx_play(flappy_sfx_over, flappy_sfx_over_len);
}

/* ---- input ------------------------------------------------------------- */

static void on_tap(lv_event_t *e)
{
    (void)e;
    switch (s_state)
    {
    case ST_READY:
        enter_play();
        sfx_play(flappy_sfx_flap, flappy_sfx_flap_len);
        break;
    case ST_PLAY:
        s_bird_vel = JUMP_V;
        sfx_play(flappy_sfx_flap, flappy_sfx_flap_len);
        break;
    case ST_OVER:
        enter_ready();
        sfx_play(flappy_sfx_ready, flappy_sfx_ready_len);
        break;
    }
}

/* ---- game loop --------------------------------------------------------- */

static bool hit_test(void)
{
    float top = s_bird_y - BIRD_HALF;
    float bottom = s_bird_y + BIRD_HALF;

    /* Frame: the whole display edge is lethal (there is no ground sprite). */
    if (top <= 0 || bottom >= s_h)
    {
        return true;
    }

    int bl = s_bird_x - BIRD_HALF;
    int br = s_bird_x + BIRD_HALF;
    for (int i = 0; i < NUM_PIPES; i++)
    {
        int pl = (int)s_pipes[i].x;
        int pr = pl + PIPE_W;
        if (br < pl || bl > pr)
        {
            continue;
        }
        int gt = s_pipes[i].gap_top;
        if (top < gt || bottom > gt + PIPE_GAP)
        {
            return true;
        }
    }
    return false;
}

static void step_play(float dt)
{
    s_bird_vel += GRAVITY * dt;
    if (s_bird_vel > MAX_FALL)
    {
        s_bird_vel = MAX_FALL;
    }
    s_bird_y += s_bird_vel * dt;

    for (int i = 0; i < NUM_PIPES; i++)
    {
        pipe_t *p = &s_pipes[i];
        p->x -= PIPE_SPEED * dt;

        if (p->x + PIPE_W < 0)
        {
            /* Recycle to the right of the furthest pipe. */
            float maxx = 0;
            for (int j = 0; j < NUM_PIPES; j++)
            {
                if (s_pipes[j].x > maxx)
                {
                    maxx = s_pipes[j].x;
                }
            }
            p->x = maxx + PIPE_SPACING;
            p->gap_top = random_gap_top();
            p->scored = false;
        }
        place_pipe(p);

        if (!p->scored && p->x + PIPE_W < s_bird_x - BIRD_HALF)
        {
            p->scored = true;
            s_score++;
            set_score_text();
        }
    }

    position_bird();

    if (hit_test())
    {
        enter_over();
    }
}

static void step_ready(void)
{
    /* Gentle hover so the bird reads as alive while waiting. */
    static float t;
    t += FRAME_MS / 1000.0f;
    s_bird_y = s_h * 0.42f + 10.0f * sinf(t * 3.0f);
    position_bird();
}

static void loop_cb(lv_timer_t *timer)
{
    (void)timer;

    if (s_state == ST_OVER)
    {
        return; /* nothing moves on the game-over screen; wait for a tap */
    }

    s_frame_ms_acc += FRAME_MS;
    if (s_frame_ms_acc >= BIRD_FRAME_MS)
    {
        s_frame_ms_acc = 0;
        set_bird_frame((s_frame + 1) % 3);
    }

    if (s_state == ST_PLAY)
    {
        step_play(FRAME_MS / 1000.0f);
    }
    else if (s_state == ST_READY)
    {
        step_ready();
    }
}

/* ---- build / lifecycle ------------------------------------------------- */

static lv_obj_t *make_pipe(lv_obj_t *parent, const lv_image_dsc_t *src)
{
    lv_obj_t *img = lv_image_create(parent);
    lv_obj_remove_style_all(img);
    lv_image_set_src(img, src);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
    return img;
}

static void on_tile_delete(lv_event_t *e)
{
    (void)e;
    if (s_timer)
    {
        lv_timer_del(s_timer);
        s_timer = NULL;
    }
    s_running = false;
    s_tile = s_bird = s_score_lbl = s_best_lbl = s_ready_img = s_over_img = s_bezel_img = NULL;
    for (int i = 0; i < NUM_PIPES; i++)
    {
        s_pipes[i].top = s_pipes[i].bottom = NULL;
    }
}

void muse_flappy_ui_build(lv_obj_t *tile)
{
    s_tile = tile;
    s_w = lv_display_get_horizontal_resolution(NULL);
    s_h = lv_display_get_vertical_resolution(NULL);
    s_bird_x = (int)(s_w * 0.32f);

    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(tile, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);

    /* Pipes (bottom of the stack). */
    for (int i = 0; i < NUM_PIPES; i++)
    {
        s_pipes[i].top = make_pipe(tile, &flappy_pipe_top);
        s_pipes[i].bottom = make_pipe(tile, &flappy_pipe_bottom);
    }

    /* Bird: swaps between three 48x48 frames for the wing flap. */
    s_bird = lv_image_create(tile);
    lv_obj_remove_style_all(s_bird);
    lv_image_set_src(s_bird, s_bird_frames[1]);
    lv_obj_set_size(s_bird, BIRD_SIZE, BIRD_SIZE);
    lv_obj_remove_flag(s_bird, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* Score (white, top-centre). */
    s_score_lbl = lv_label_create(tile);
    lv_obj_remove_style_all(s_score_lbl);
    lv_obj_set_style_text_font(s_score_lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_score_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_score_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_score_lbl, "0");
    lv_obj_align(s_score_lbl, LV_ALIGN_TOP_MID, 0, s_h / 8);

    /* Get Ready / Game Over overlays (under the bezel). */
    s_ready_img = lv_image_create(tile);
    lv_obj_remove_style_all(s_ready_img);
    lv_image_set_src(s_ready_img, &flappy_getready);
    lv_obj_align(s_ready_img, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(s_ready_img, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    s_over_img = lv_image_create(tile);
    lv_obj_remove_style_all(s_over_img);
    lv_image_set_src(s_over_img, &flappy_gameover);
    lv_obj_align(s_over_img, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_over_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_over_img, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* "BEST n", on top of the Game Over banner (created after it so it shows). */
    s_best_lbl = lv_label_create(tile);
    lv_obj_remove_style_all(s_best_lbl);
    lv_obj_set_style_text_font(s_best_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_best_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_best_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_best_lbl, "BEST  0");
    lv_obj_align(s_best_lbl, LV_ALIGN_CENTER, 0, (int)(s_h * 0.32f));
    lv_obj_add_flag(s_best_lbl, LV_OBJ_FLAG_HIDDEN);

    /* Bezel on top; its transparent middle lets the game show through. */
    s_bezel_img = lv_image_create(tile);
    lv_obj_remove_style_all(s_bezel_img);
    lv_image_set_src(s_bezel_img, &flappy_bezel);
    lv_obj_align(s_bezel_img, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(s_bezel_img, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* A tap anywhere on the tile flaps / starts / restarts. */
    lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tile, on_tap, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(tile, on_tile_delete, LV_EVENT_DELETE, NULL);

    /* One long-lived sound task for the whole UI lifetime. */
    if (!s_sfx_q)
    {
        s_sfx_q = xQueueCreate(3, sizeof(sfx_req_t));
    }
    if (s_sfx_q && !s_sfx_task)
    {
        xTaskCreate(sfx_task, "flappy_sfx", 3072, NULL, 4, &s_sfx_task);
    }

    load_high();
    enter_ready();
}

void muse_flappy_ui_tick(bool visible)
{
    if (!s_tile)
    {
        return;
    }
    if (visible && !s_running)
    {
        if (!s_timer)
        {
            s_timer = lv_timer_create(loop_cb, FRAME_MS, NULL);
        }
        s_running = true;
        /* Opening the page always lands on "get ready": announce it. */
        if (s_state == ST_READY)
        {
            sfx_play(flappy_sfx_ready, flappy_sfx_ready_len);
        }
    }
    else if (!visible && s_running)
    {
        if (s_timer)
        {
            lv_timer_del(s_timer);
            s_timer = NULL;
        }
        s_running = false;
        /* Leaving the page resets it to a clean "get ready". */
        enter_ready();
    }
}
