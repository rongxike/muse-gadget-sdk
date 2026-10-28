<!--
Copyright (c) Meta Platforms, Inc. and affiliates.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->

# Devices

These are the boards the ESP32 Device SDK runs on. Each board is an `sdkconfig`
overlay: a short file of settings loaded on top of
[`../sdkconfig.defaults`](../sdkconfig.defaults) that picks the chip, flash,
memory, button, and how the device shows its status. The ESP32-C5 DevKitC-1 is
the default and needs no overlay.

Every board pairs with the Muse app, joins your Wi-Fi, and holds an encrypted
session to Muse. The rest depends on the hardware.

## Supported devices

| Board                                     | Chip     | Display                           | Flash / PSRAM | Reference                                                                                                                                        | Buy                                                                    |
| ----------------------------------------- | -------- | --------------------------------- | ------------- | ------------------------------------------------------------------------------------------------------------------------------------------------ | ---------------------------------------------------------------------- |
| **Waveshare ESP32-S3-Touch-AMOLED-1.75C** | ESP32-S3 | 1.75" 466×466 round AMOLED, touch | 32 MB / 8 MB  | [Waveshare wiki](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75C), [GitHub](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75C) | [Waveshare](https://www.waveshare.com/esp32-s3-touch-amoled-1.75c.htm) |

## Features

|                      | DevKitC-1 | C6 devkit | ideaspark | Waveshare C6 LCD 1.47 | SenseCAP Indicator | Waveshare S3 ePaper 1.54 |   reTerminal E1001    |   reTerminal E1002    |     HA Voice PE     | reSpeaker Lite (experimental) | Waveshare S3 1.75C | Waveshare S3 1.75 |   Waveshare S3 1.85C   |          Waveshare S3 2.16          | AIPI Lite | Waveshare C6 1.8 | Waveshare C6 2.06 |               Watcher                |            StickS3             |          StickC Plus2          |              Cardputer ADV               |       BOX-3        |                                 StopWatch                                 |     CoreS3      |         Core2          |   FNK0104B   | S3 DevKitC-1 |        JC3248W535        |        VN S3 1.83-inch        |  Waveshare LCD7   |    Waveshare LCD 1.28     |         AI Passport         |       EchoEar-2ST\*        |
| -------------------- | :-------: | :-------: | :-------: | :-------------------: | :----------------: | :----------------------: | :-------------------: | :-------------------: | :-----------------: | :---------------------------: | :----------------: | :---------------: | :--------------------: | :---------------------------------: | :-------: | :--------------: | :---------------: | :----------------------------------: | :----------------------------: | :----------------------------: | :--------------------------------------: | :----------------: | :-----------------------------------------------------------------------: | :-------------: | :--------------------: | :----------: | :----------: | :----------------------: | :---------------------------: | :---------------: | :-----------------------: | :-------------------------: | :------------------------: |
| Home-network tunnel  |    ✅     |     —     |     —     |           —           |         ✅         |            ✅            |          ✅           |          ✅           |         ✅          |              ✅               |         ✅         |        ✅         |           ✅           |                 ✅                  |    ✅     |        —         |         —         |                  ✅                  |               ✅               |               ✅               |                    —                     |         ✅         |                                    ✅                                     |       ✅        |           ✅           |      ✅      |      ✅      |            ✅            |              ✅               |        ✅         |            ✅             |              —              |          Enabled           |
| Shows status on      |   Light   |   Light   |  Screen   |        Screen         |       Screen       |         E-paper          |        E-paper        |        E-paper        |     Light ring      |            RGB LED            |       Avatar       |      Avatar       |         Avatar         |               Avatar                |  Avatar   |      Avatar      |      Avatar       |                Avatar                |             Avatar             |             Avatar             |                  Avatar                  |       Avatar       |                                  Avatar                                   |     Avatar      |         Avatar         |    Avatar    |    Light     |          Avatar          |            Avatar             |      Avatar       |          Avatar           |           Avatar            |           Avatar           |
| Images from Muse     |     —     |     —     |    ✅     |          ✅           |         ✅         |     Black and white      |    Black and white    |      Six colours      |          —          |               —               |         ✅         |        ✅         |           ✅           |                 ✅                  |    ✅     |        —         |         —         |                  ✅                  |               ✅               |               ✅               |                    —                     |         ✅         |                                    ✅                                     |       ✅        |           ✅           |      ✅      |      —       |            ✅            |              ✅               |        ✅         |            ✅             |              —              |          Enabled           |
| UI and settings      |     —     |     —     |     —     |           —           |         —          |            —             |           —           |           —           |          —          |               —               |         ✅         |        ✅         |           ✅           |                 ✅                  |    ✅     |        ✅        |        ✅         |                  ✅                  |               ✅               |               ✅               |               Experimental               |         ✅         |                                    ✅                                     |       ✅        |           ✅           |      ✅      |      —       |            ✅            |              ✅               |        ✅         |            ✅             |        Experimental         |        Experimental        |
| Push-to-talk         |     —     |     —     |     —     |           —           |         —          |            —             |           —           |           —           |         ✅          |           XIAO BOOT           |         ✅         |        ✅         |           ✅           |                 ✅                  |    ✅     |   Text replies   |   Text replies    |                  ✅                  |               ✅               |               ✅               |       Text replies (experimental)        |         ✅         |                                    ✅                                     |       ✅        |           ✅           |      ✅      |      —       |  With an added I2S mic   |              ✅               |         —         |             —             | Text replies (experimental) |         Screen mic         |
| Speaker and mic      |     —     |     —     |     —     |           —           |         —          |            —             |           —           |           —           |         ✅          |          16 kHz I2S           |         ✅         |        ✅         |           ✅           |                 ✅                  |    ✅     |        ✅        |        ✅         |                  ✅                  |               ✅               |         Buzzer and mic         |          ES8311 (experimental)           |         ✅         |                                    ✅                                     |       ✅        |           ✅           |    ES8311    |      —       | Speaker (NS4168), no mic |        ES8311, ES7210         |         —         |             —             |    ES8311 (experimental)    |      ES8311 / ES7210       |
| Air sensors          |     —     |     —     |     —     |           —           |     D1S, D1Pro     |            —             | Temperature, humidity | Temperature, humidity |          —          |               —               |         —          |         —         |           —            |                  —                  |     —     |        —         |         —         |                  —                   |               —                |               —                |                    —                     |         —          |                                     —                                     |        —        |           —            |      —       |      —       |            —             |               —               |         —         |             —             |              —              |             —              |
| Touch                |     —     |     —     |     —     |           —           |         —          |            —             |           —           |           —           |          —          |               —               |         ✅         |        ✅         |           ✅           |                 ✅                  |     —     |        ✅        |        ✅         |                  ✅                  |               —                |               —                |                    —                     |         ✅         |                                    ✅                                     |       ✅        |           ✅           |      ✅      |      —       |            ✅            |               —               |        ✅         |            ✅             |              —              |             ✅             |
| Battery status       |     —     |     —     |     —     |           —           |         —          |            —             |           —           |           —           |          —          |               —               |         ✅         |        ✅         |           ✅           |                 ✅                  |    ✅     |        ✅        |        ✅         |                  ✅                  |               ✅               |          Voltage only          |                    —                     |         —          |                                    ✅                                     |       ✅        |           ✅           | Voltage only |      —       |            —             |         Percent only          |         —         | Voltage only (unverified) |      No charging state      |         Unverified         |
| Over-the-air updates |    Off    |    Off    |    Off    |          Off          |        Off         |           Off            |          Off          |          Off          |         Off         |              Off              |         On         |        On         |           On           |                 On                  |    On     |        On        |        On         |                  On                  |               On               |               On               |                   Off                    |         On         |                                    On                                     |       On        |           On           |      On      |     Off      |            On            |              On               |        On         |            On             |             Off             |             On             |
| Buttons              |   BOOT    |   BOOT    |   BOOT    |         BOOT          |        Top         |           BOOT           |         Green         |         Green         | Centre (talk), dial |    XIAO BOOT (talk/setup)     |  PWR (talk), BOOT  | BOOT (talk), PWR  | Hold face (talk), BOOT | BOOT, PWR (talk), KEY (talk) on top |    Two    | BOOT (talk), PWR | BOOT (talk), PWR  | Wheel (press to talk, turn to sleep) | Front (talk), side (menu), PWR | Front (talk), side (menu), PWR | GO/Space (talk), Esc/Enter/arrows (menu) | BOOT/CONFIG (talk) | Blue top-right (talk), yellow top-left (speaker), red bottom-left (power) | PWR (talk), RST | Touch BtnB (talk), PWR | BOOT (talk)  |     BOOT     |       BOOT (talk)        | BOOT (talk), Vol+/Vol- (menu) | BOOT (flash only) |        Screen only        |  OK (talk), UP/DOWN (menu)  | Touch; bottom power button |

\*EchoEar-2ST entries describe the one measured unit, tested on hardware for
pairing, touch, recording and playback, sleep and wake, and reconnect. See
[validation limits](ostb-echoear-2st.md#validation).

Boards without PSRAM (the ideaspark, the C6 boards, the Cardputer ADV and the AI Passport) don't have room for
the home-network tunnel. Muse can still reach and control them once the
control session is up. The Waveshare C6, Cardputer ADV and AI Passport also can't hold their own voice
session, so push-to-talk sends your voice note over its control session to the
Muse it's paired with, and the reply scrolls past as text. It can't show images either: the UI holds a whole image in
PSRAM, where the ideaspark draws one straight to its screen.

The Waveshare 7-inch board is a display and touch gadget with no built-in
microphone or speaker. Tap the screen to confirm pairing in the Muse app.
The backlight has an on/off switch rather than dimming control. Its BOOT key
shares GPIO0 with the RGB panel, so it is reserved for entering the bootloader
and is not a Muse input while the display runs.

The Waveshare 1.28-inch board is also display and touch only: no microphone,
no speaker, and no button wired to Muse. Tap the screen to confirm pairing.
Its ESP32-S3R2 has 2 MB of PSRAM, so unlike the 8 MB boards its code runs
from flash rather than PSRAM. Its console is on a CH343P bridge (UART0), not
the chip's own USB.

The SenseCAP Indicator's sensors hang off its RP2040, which passes the
readings to the ESP32-S3. The D1S and D1Pro have CO2 and tVOC sensors built
in, and temperature and humidity come from the Grove AHT20 in the box (plug it
in). Muse reads them all at once with `sensors.read`. This needs Seeed's stock
RP2040 firmware.

The reTerminal E1001's e-paper shows only black and white, 1 bit per pixel,
and keeps its picture without power. It shows a still status screen (the
agent's name, the character and a line of status text) that changes only when
the status does, since each refresh takes a second or two. Images from Muse are dithered
to black and white on the device and refresh once they are all in, with a
brief black-and-white flash. `display.draw_url` tells Muse the bit depth.

The reTerminal E1002 is the same board with a six-colour E Ink Spectra 6
panel: black, white, yellow, red, blue and green, 4 bits per pixel. It shows
the same status screen, with the character in colour. Images from Muse are
dithered to those six inks on the device (grays to black and white only),
and each refresh takes about 30 seconds and flashes. `display.draw_url` tells
Muse the six exact colours, the resolution and how slow it is.

Both reTerminals have an SHT4x temperature and humidity sensor on board,
which Muse reads with `sensors.read`. The board warms the sensor a little, so
the temperature is corrected by `CONFIG_HOMEHUB_RETERMINAL_SHT4X_TEMP_OFFSET`
(in tenths of a degree, -3.3 °C by default).

The SenseCAP Watcher keeps its factory data (the identity SenseCraft uses) in
an `nvsfactory` partition at `0x9000`, where Muse puts its partition table and
NVS. It's unique to each Watcher, so back it up before you flash Muse the first
time. To return to Seeed's firmware, flash it and then write the backup back:

```sh
tools/muse/paced_esptool.py --chip esp32s3 -p PORT read-flash 0x9000 0x32000 nvsfactory.bin
tools/muse/paced_esptool.py --chip esp32s3 -p PORT write-flash 0x9000 nvsfactory.bin
```

The Watcher's CH342 USB bridge corrupts reads at 921600 baud and above, so
these use esptool's default of 115200. It also drops bytes when a whole packet
arrives at once, so plain esptool can't upload its stub or write flash
(`0107: Checksum error`, `0105: The format of the received message is
invalid`). `tools/muse/paced_esptool.py` takes esptool's arguments and sends 64
bytes at a time at the line rate; `tools/muse/board.sh flash watcher` uses it.

On battery with the screen off and voice resting, the Watcher cuts the LCD
rail, suspends both codecs and the speaker amp, and lets the ESP32-S3
light-sleep with its CPU powered down until the wheel or an expander input
wakes it. CPU power-down saves about 0.44 mA of a 2.96 mA rest. Its retention
memory, about 15 KB of internal RAM, is taken at boot and kept, and comes from
the avatar's and audio's static buffers, which this profile keeps in PSRAM
(`CONFIG_MUSE_STATIC_BUFFERS_IN_PSRAM`). `tools/muse/power.py` shows the last
run on battery, with the time actually slept, the power locks that kept the
chip awake (`muse_awake` is Muse itself, whenever it isn't resting), and
internal RAM: free, lowest since boot, lowest while resting, what's left of
the RAM retention memory can use, and whether the CPU powers down. The
retention memory is already out of all of these, so resting shows no dip of
its own: its lowest is the headroom Wi-Fi, BLE and the tunnel had while the
board rested. The heartbeat log (`link.heartbeat`) prints the same as
`int=free/largest min=lowest ret=free/largest cpu_pd=on|off`.

The M5Stack StickS3 has 8 MB of flash, so it uses its own partition table with
two smaller app slots. The front button is push-to-talk and the side button
steps through the menu, as on the AIPI. Powering off from Muse turns off the
screen, speaker and codec and puts the ESP32-S3 in deep sleep; either button
wakes it. Double-click the power button for a full power-off, and click it to
turn back on. Muse turns off the power chip's green LED, which would otherwise
stay lit. The IMU, IR and Grove port aren't used yet.
[M5Unified](https://github.com/m5stack/M5Unified) is M5Stack's reference
driver for the power chip and peripherals.

M5Stack ships the StickS3 with UiFlow2, which hands the ESP32-S3's USB to its
own driver and switches off the chip's USB serial port, so esptool can't find
it. The power chip drives the boot pin (GPIO0), so there's no BOOT button
either. To flash Muse the first time, put UiFlow2 in USB mode, open its REPL
(for example with `mpremote repl`) and paste:

```python
import machine; m = machine.mem32
m[0x600C001C] = m[0x600C001C] | (1 << 10)                 # USB serial clock on
m[0x600C0024] = m[0x600C0024] & ~(1 << 10)                # and out of reset
m[0x60038018] = 0x4200                                    # its pads on
m[0x60008120] = (m[0x60008120] | (1 << 20)) & ~(1 << 19)  # give it the USB pins
```

The REPL stops answering. Unplug the USB cable and plug it back in (the
battery keeps the stick running), and it shows up as a USB JTAG/serial port.
Anything that resets the stick now boots UiFlow2 again, so pass
`--after no-reset` to esptool until Muse is on. Back up the flash, then flash
Muse:

```sh
python -m esptool --chip esp32s3 -p PORT --after no-reset read-flash 0 0x800000 sticks3.bin
tools/muse/board.sh flash sticks3 PORT
```

Muse keeps the USB serial port on, so later flashes need none of this. To go
back to UiFlow2, write the backup:

```sh
python -m esptool --chip esp32s3 -p PORT write-flash 0 sticks3.bin
```

The M5Stack StopWatch has the same CO5300 round AMOLED as the Waveshare S3
1.75C, with a CST820 touch controller and the StickS3's ES8311 audio. With USB
at the bottom, **blue top-right is PTT/pairing confirmation**, **yellow
top-left toggles speaker on/off**, and **red bottom-left is POWER**. A short
POWER press sleeps/wakes the screen; a 1.5 s hold while awake shows goodbye
and shuts down the M5PM1 (click red to power back on). A waking press is
consumed, not interpreted as another action. Hardware double-click power-off
and download access remain available; Muse disables single-click reset, as
the factory does, and extends the PMIC long delay to 4 s. Settings are one
swipe left from Muse. Icon-only physical hints (microphone, speaker/mute and
power) stay beside the rim on the face, reply and pairing views, but are hidden
in settings so they don't cover controls. Speaker/mute follows the saved
speaker setting; the microphone brightens while recording. The green PMIC
power LED is off on battery and on with USB power, updated at boot and with
the normal battery readings (including while the screen sleeps). This changes
only M5PM1 `PWR_CFG` (`0x06`) bit 4, preserving charging and the power rails.
M5's IO expander (M5IOE1) switches the panel and touch resets, audio power
and the amp; its power chip (M5PM1) reads the battery, charger and red key's
live state (register `0x48`, bit 0). Asleep on battery, the OLED's rail
(3V3_L3B) and the audio rail (AU_L3B: codec and mic) are off and the chip
light-sleeps until a rim key, or the M5PM1's click interrupt on GPIO12, wakes
it. The red key's click arrives once it's released (about 125 ms later), and a
press held at rest is found within a second, also while Wi-Fi naps. Waking,
the panel takes about half a second longer to come back, and the first
recording after a rest starts about 0.1 s later while the codec is set up
again. The IMU is put back in suspend at boot; it, the RTC, vibration motor
and Grove port aren't used yet.

Bench builds also accept `>ui=settings` and `>ui=face` for touch-tile
screenshots (`a/s` only navigate non-touch menus). `>face=boot`, `idle`,
`listening`, `thinking`, `speaking`, `error` or `off` selects the face state;
`>speaker=0` / `>speaker=1` exercises both speaker indications. For privacy-safe
unpaired captures, `>ui.demo=1` shows `MuseGadget-DEMO` instead of the hardware
name; `>ui.demo=0` restores it. This changes only the screen, not BLE identity,
pairing, Wi-Fi or tokens.

```sh
python -m esptool --chip esp32 -p PORT -b 230400 read-flash 0 0x800000 plus2.bin
```

To go back, write the backup with `write-flash 0 plus2.bin`, using the same
chip, port and baud.

## ESP32-S3-DevKitC-1

The [S3 DevKitC-1 overlay](sdkconfig.espressif-s3-devkitc-1) defaults to
GPIO38 for genuine Espressif v1.1 boards. GPIO48 was verified on an Aideepen
ESP32-S3 N16R8 DevKitC-1 clone. For this clone, set
`CONFIG_HOMEHUB_LED_STRIP_GPIO=48` in the build directory's `sdkconfig`
and then rebuild and flash.

In the hardware check, a standalone WS2812 blink sketch blinked blue three
times on GPIO48 and produced no light on GPIO38. With the override, Muse
Gadget breathed orange while advertising. Serial logged
`LED status ready: addressable RGB (GPIO=48)` and heartbeats with
`ble=advertising`, with no panic.

## Build

From the `esp32` directory, load `sdkconfig.defaults` first and then the
board's overlays, in order:

| Board              | Target    | Overlays after `sdkconfig.defaults`                                                                                        | Build   |
| ------------------ | --------- | -------------------------------------------------------------------------------------------------------------------------- | ------- |
| Waveshare S3 1.75C | `esp32s3` | [`devices/sdkconfig.muse`](sdkconfig.muse), [`devices/sdkconfig.muse-waveshare-s3-175c`](sdkconfig.muse-waveshare-s3-175c) | by hand |

`tools/board.sh BOARD [build|flash|monitor|flash-monitor] [PORT]` builds each
board in its own `build-<board>` directory. For the boards with the full UI, run `idf.py`
with the target and overlays from the table:

```sh
idf.py -B build-muse-aipi -DIDF_TARGET=esp32s3 \
  -DSDKCONFIG=build-muse-aipi/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;devices/sdkconfig.muse;devices/sdkconfig.muse-aipi" build
```

To flash, add `-p PORT flash` with the same arguments. Boards with the full UI need 16 MB
of flash or more, except the StickS3, StickC Plus2, Cardputer ADV and AI Passport, whose overlays switch
to the 8 MB layout in [`partitions_muse_8mb.csv`](../partitions_muse_8mb.csv).
[`AGENTS.md`](../AGENTS.md) covers flashing, monitoring, and what to do when a
build picks up stale settings.

## Waveshare ESP32-S3-Touch-LCD-1.85C

The 1.85C is a 1.85" 360x360 round LCD (ST77916, QSPI) with CST816 capacitive
touch, an ES8311 speaker codec and an ES7210 microphone ADC sharing one I2S
bus, and a 200K/100K battery divider on GPIO8. It runs the full avatar UI:
hold a finger on the face tile to talk, BOOT for push-to-talk and
pairing confirmation, settings on the touchscreen.

It enumerates as the chip's own USB Serial/JTAG port; there's no USB bridge:

```
tools/muse/board.sh build 175c
tools/muse/board.sh flash 175c
```

Back up the original 16 MB firmware before flashing Muse the first time, and
keep the backup outside Git:

```
python -m esptool --chip esp32s3 -p PORT -b 460800 read-flash 0 0x1000000 waveshare-s3-175c-backup.bin
```

Restore with `write-flash 0 waveshare-s3-175c-backup.bin`, using the same
chip, port and baud.

## Add a board

1. Copy the closest overlay here as `sdkconfig.<yourboard>`.
2. Set at least `CONFIG_IDF_TARGET`, `CONFIG_HOMEHUB_BUTTON_GPIO`, the status
   backend (`CONFIG_HOMEHUB_LED_BACKEND_*`), and the flash size and mode.
3. Without PSRAM, also turn off `CONFIG_SPIRAM` and `CONFIG_HOMEHUB_TUNNEL` and
   keep mbedtls in internal memory, as [`sdkconfig.ideaspark`](sdkconfig.ideaspark)
   does.
4. Build with `SDKCONFIG_DEFAULTS="sdkconfig.defaults;devices/sdkconfig.<yourboard>"`
   and add your board to the tables above.

A board with a new kind of display, or one that runs the UI, needs code
as well. [`AGENTS.md`](AGENTS.md) walks through every kind of board, from the
overlay to testing on hardware.

Got it working on something new? Share it in the
[Muse Gadgets Discord](https://discord.gg/3bhjCkZdd6).

### Watcher camera

Camera support is disabled by default. In the Watcher build's `menuconfig`,
under **Muse**, enable **SenseCAP Watcher camera capture and live preview**
(`CONFIG_MUSE_WATCHER_CAMERA=y`) and rebuild. It requires PSRAM. Disabled
builds omit the camera worker, shutter UI, double-click gesture, and
`camera.capture` command.

Double-click the wheel to open a live camera view. Aim the Watcher, then tap
**TAP TO TAKE PHOTO** or double-click the wheel again. The captured frame stays
on screen. Tap the image to return to the avatar.

The `camera.capture` command returns a JPEG in
`payload.data_base64`, with `payload.format` set to `jpeg-base64`. During live
preview it returns the latest frame. Otherwise it requests a fresh frame.
Capture runs asynchronously and reports initialization, timeout, or busy errors.
The camera uses the existing Himax firmware. It's powered only while a capture
or the live view runs (a capture takes under a second, start-up included), and
nothing polls it in between.
Photo attachments to voice messages are not included.
