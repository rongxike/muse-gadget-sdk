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
- Current task: the clock swipe lag/memory came from rendering the whole neon face every frame (144 arcs + 60 ticks + specks in `on_bg_draw`) plus decoding a PNG background. This is now replaced by the single baked `clock_face` flash image (no decode, direct blit) with only the hands/date drawn live.
- Build/flash requirement: before flashing, update the firmware version text and keep this task remembered in the agent instructions. The display runs `LV_COLOR_FORMAT_RGB565_SWAPPED` (muse_lcd_bands.c); baked images must match that color format for a zero-conversion blit.
- Prefer targeted, minimal edits that preserve existing patterns and user-facing behavior unless a requirement explicitly changes it.

# Build and Flash

Always build and flash using the specified serial port. only use -BuildOnly if you are asked

```ps1
.\scripts\deploy-routellm.ps1 -Port COM4
```
