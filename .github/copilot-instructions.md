# Muse Gadget SDK Instructions
- Target device: Waveshare 175C.
- Keep behavior changes in the UI/input layers; avoid board core or hardware-driver changes unless explicitly requested.
- Clock page requirements:
  - Do not use fireflies.
  - The clock face (sky, bezel, tick marks, Roman numerals and cardinal markers) is a single pre-baked image, `clock_face.c` (an LVGL `RGB565_SWAPPED` `lv_image_dsc_t` named `clock_face`, generated from `clock_face.png` via LVGL's `scripts/LVGLImage.py`). It is blitted directly from flash; do not re-add per-frame vector rendering (arcs/ticks/specks) for the face.
  - Only the hour/minute hands, the second bar and the date label are live objects over the baked face.
  - The second hand must be a short white rim bar.
  - Date format must be `dd-MMM-yyyy` with small rainbow text.
  - Keep the page movement smooth.
- Navigation requirement:
  - A right-to-left swipe from the clock page must return to the AI main page.
- Preserve existing wake behavior:
  - `sleep talk` press wakes only.
  - The next press starts talk.
- Memory and stability guidance:
  - Avoid high-frequency animations.
  - Stop non-essential animation after idle.
- Flappy Bird game page (CONFIG_MUSE_FLAPPY, far-right tile, column 3):
  - Native LVGL port of externals/esp32_flappybird (not the web-server .ino).
  - Art is baked in-app as LVGL images via scripts/LVGLImage.py (flappy_*.c):
    bezel/bird/pipes are RGB565A8, Get Ready / Game Over are RGB565_SWAPPED.
    Source PNGs live in assets/flappy-bird/. The background is just a black tile
    (no bg image); the bird is three 48x48 frames; the bezel overlays on top.
  - Chosen over a dedicated assets partition because removing the CJK font freed
    enough app-slot space to bake the art in (keeps the proven clock_face path,
    no esp_mmap_assets/esp_lv_fs/deploy-script changes).
  - Tap anywhere to flap; a right-to-left swipe returns toward Muse. The game
    loop runs only while the tile is visible (muse_flappy_ui_tick) and resets on
    hide. High score is kept in NVS ("flappy"/"hi").
  - Sounds (flappy_sfx.c): the 3 mp3s in assets/flappy-bird/ are pre-decoded on
    the host (miniaudio) to 16 kHz mono PCM, embedded, and played by a dedicated
    FreeRTOS task via muse_audio_write so the game loop never blocks. flap-wings
    on each flap, game-over on death, game-ready when the Get Ready screen opens.
  - Page freezing: every page's per-frame work is gated by its *_ui_tick(visible)
    and only the active tile ticks. update_sleep() also calls every page's
    tick(false) when the panel sleeps, so a page timer never renders to a
    powered-down panel (that caused a hang when idling on the game page).
  - The Chinese/Japanese caption font (CONFIG_MUSE_CJK_FONT) is intentionally
    removed from the RouteLLM build; captions fall back to UNSCII-16.
- Current task: the clock swipe lag/memory came from rendering the whole neon face every frame (144 arcs + 60 ticks + specks in `on_bg_draw`) plus decoding a PNG background. This is now replaced by the single baked `clock_face` flash image (no decode, direct blit) with only the hands/date drawn live.
- Build/flash requirement: before flashing, update the firmware version text and keep this task remembered in the agent instructions. The display runs `LV_COLOR_FORMAT_RGB565_SWAPPED` (muse_lcd_bands.c); baked images must match that color format for a zero-conversion blit.
- Prefer targeted, minimal edits that preserve existing patterns and user-facing behavior unless a requirement explicitly changes it.

# Build and Flash

Always build and flash using the specified serial port. only use -BuildOnly if you are asked

```ps1
.\scripts\deploy-routellm.ps1 -Port COM4
```
