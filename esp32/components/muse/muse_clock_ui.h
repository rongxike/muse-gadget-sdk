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
 * The clock tile (swipe right from Muse): a neon analog clock over an optional
 * sky background image. Static artwork (bezel, ticks, cardinal markers, specks)
 * is drawn with lightweight LVGL vector draws, and only the hands/date update.
 *
 * Runs entirely in the LVGL task. Page-owned timers start only while the tile
 * is visible and stop when it is hidden, so a hidden page does no work.
 */

/* Build the clock once into `tile`. Starts no timers. */
void muse_clock_ui_build(lv_obj_t *tile);

/* Call periodically from the LVGL task; `visible` = clock tile is showing.
 * Starts the hand and firefly timers on the hidden->visible edge (refreshing
 * the time immediately) and stops them on the visible->hidden edge. */
void muse_clock_ui_tick(bool visible);
