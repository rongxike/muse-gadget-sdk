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

#pragma once

#include <stdbool.h>

#include "lvgl.h"

/*
 * The Flappy Bird tile (swipe left from settings, far right of Muse): a neon
 * mini-game drawn with pre-baked LVGL images over a black background and under
 * a transparent bezel overlay. Tap anywhere to flap; a right-to-left swipe
 * leaves the page as usual.
 *
 * Runs entirely in the LVGL task. The game loop timer starts only while the
 * tile is visible and stops when it is hidden, so a hidden page does no work.
 */

/* Build the game once into `tile`. Starts no timers. */
void muse_flappy_ui_build(lv_obj_t *tile);

/* Call periodically from the LVGL task; `visible` = the flappy tile is showing.
 * Starts the game loop on the hidden->visible edge and stops it (pausing the
 * game back to its "get ready" state) on the visible->hidden edge. */
void muse_flappy_ui_tick(bool visible);
