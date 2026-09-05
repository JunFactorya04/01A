# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Geopix — an ESP32-S3 firmware for an automatic wildlife/traffic camera trigger box. A LovyanGFX
240x135 display + rotary encoder + power button form the UI; a Benewake TF-Luna LiDAR detects
objects at a configured distance and fires a camera via two GPIO outputs (and optionally BLE).
Framework: Arduino, built with PlatformIO. Board: `esp32-s3-devkitc-1`.

## Commands

PlatformIO CLI may not be on `PATH`; if `pio` is not found, use `~/.platformio/penv/bin/pio`.

```bash
pio run                                    # build
pio run --target upload                    # build + flash
pio run --target upload --upload-port /dev/cu.usbserial-XXXX   # flash to a specific port (macOS)
pio device list                            # list serial ports
pio device monitor                         # serial monitor (115200 baud, esp32_exception_decoder
                                            # filter is configured — crash addresses get decoded
                                            # into function names/lines automatically)
pio run --target clean                     # clean build artifacts (.pio/build/)
```

There is no test suite (`test/` is an empty PlatformIO placeholder) and no linter configured —
verification is "does it build" (`pio run`) plus manual testing on hardware.

`upload_speed` in `platformio.ini` is pinned to `460800` (not the board's 921600 default) because
the higher speed reliably produced `Serial data stream stopped: Possible serial noise or
corruption` on this hardware setup. Drop to `115200` if uploads still fail.

Git history starts at commit `checkpoint: stable baseline before TF-Luna I2C frame-rate change`
(tag `stable-before-fps-change`) — there is no earlier history. Before a change with real
hardware-regression risk (sensor timing/registers, power-on sequencing, anything hard to verify
without flashing a real board), commit or tag the current known-good state first so it's a clean
`git reset --hard <tag>` away if the change destabilizes something on hardware — this project has
already needed that once.

## Architecture

### Central hub + blocking per-mode loops

`FactoryTest` (`src/factory_test/factory_test.h/.cpp`) is a god-object owning every shared
peripheral: display (`_disp`/`_canvas`, a LovyanGFX sprite pushed via `_canvas_update()`),
rotary encoder (`_enc`), power button (`_btn_pwr`), RTC (`_rtc`), buzzer (`_tone()`). `main.cpp`
constructs one static `FactoryTest`, calls `init()`, plays the boot logo, then hands off to
`view.cpp`'s launcher menu (`view_create`/`view_update`, called from `loop()`).

The launcher (`view.cpp`) is a card-carousel built on the `SmoothUIToolKit` library (in `lib/`).
Selecting a card calls one of `FactoryTest::_<mode>_test()` (e.g. `_auto_shoot_test()`,
`_timelapse_test()`), each implemented in its own `src/factory_test/factory_test_<mode>.cpp`.
Every mode entry point is a **blocking `while(1)` loop** that only returns when
`_mode_exit_requested` is set (long-press exits) or the mode is done. Shared input helpers live
in `factory_test_input.cpp`: `_read_encoder_delta()` and `_read_mode_button_event()`
(short/long press via `digitalRead`, not the `Button` lib's own debounce). New modes should
follow this same `<mode>.h/.cpp` (state/config/logic) + `<mode>_ui.h/.cpp` (render) +
`factory_test_<mode>.cpp` (glue: entry point, input handling, `FactoryTest` method
implementations) three-file split.

### Per-mode singleton pattern

Each mode is a global singleton (`autoShoot`, `timelapse`, `triggerMode`, `sleepWeekScheduler`,
`setting`, `displayMode`) with its own `Config`/`State`/`EditMode` structs and
`loadConfig()`/`saveConfig()` via `Preferences` (NVS), one namespace string per mode (e.g.
`"autoShoot"`, `"timelapse"`, `"sleepWeek"`). **NVS keys are capped at 15 chars** — this has
already caused one real bug (`"schedulerEnabled"` silently failed to persist; renamed to
`"schedEn"`). Check key length when adding new persisted fields.

### Shared trigger hardware and the trigger lock

Camera triggering is dual-output: `TRIGGER_G2_PIN` (main) and `TRIGGER_G1_PIN` (backup), plus an
optional 3rd "channel" over BLE. `TriggerMode` (`src/trigger_mode/`) holds the master on/off
switches for all three channels and is treated as shared config — `AutoShoot` and `Timelapse`
both read `triggerMode.config.*` before firing rather than owning their own output enable state.
`acquireTriggerLock()`/`releaseTriggerLock()` (`src/common/hardware_config.h`) is a global mutex
preventing Auto Shoot and Timelapse from pulsing the GPIOs at the same moment — any new
camera-firing code path must acquire it around the digitalWrite HIGH/LOW pair.

### TF-Luna driver (`src/auto_shoot/tf_luna.h/.cpp`)

I2C on `Wire1` (SDA=13, SCL=15) — deliberately a separate bus from the RTC, which uses `Wire`
(bus 0). Polling is paced to the sensor's *confirmed* frame rate (`_samplePeriodMs`, derived in
`configureFrameRate()`), with backoff while failing, and self-healing (bus reinit + sensor
soft-reset) after `TFLUNA_MAX_FAILS` consecutive failures. **`sleep()`/`wake()` (register 0x25)
exist in the API but must never be called on mode entry/exit** — field testing showed writing
that register can stall the sensor for many seconds; the comment in the source explains this.

`begin()` raises the sample rate above the sensor's 100Hz default via `configureFrameRate()`,
which writes registers `0x26`/`0x27` (`TFLUNA_TARGET_FPS_HZ`, currently 200) and reads them back
to confirm before trusting the new pacing — falls back to the safe 10ms/100Hz pacing if the
write/readback doesn't check out. These addresses were verified against Benewake's official I2C
register map and the widely-used `budryerson/TFLuna-I2C` library source, and are distinct from
the `0x25` register that caused the stalls above — do not conflate the two when reasoning about
risk. `tfLuna.getFrameRateHz()` exposes the confirmed rate (0 = unconfirmed/fallback) and is shown
live in the Auto Mode header; reading it is a cached-member read, not an I2C access.

`warmUp()` (bounded burst of reads to pre-settle the sensor) is intentionally **unused/dead
code** — it was tried both at boot (`FactoryTest::init()`, right after the power latch) and
inside `AutoShoot::init()`; either placement made the sensor/board noticeably less stable on real
hardware (the boot placement also risked brownout on a weak battery — see below). If revisiting
sensor warm-up, treat it as a real regression risk, not a free win, and verify on hardware before
keeping it.

### Auto Shoot: pure mode (default) vs. the Advance range filter

`AutoShoot` has two screens (`EditMode::Screen`, same MAIN/sub-screen split as TriggerMode's
Bluetooth screen): **MAIN** (Burst, Cooldown, "Advance" row, START, STOP) and **ADVANCE**
(Range Filter ON/OFF, Range Min, Range Max). `config.filterEnabled` defaults to **false** — in
`updateSensorData()` this means `state.objectDetected = tfLuna.hasObject()` (any valid return
counts, full sensor power, no distance restriction — the "pure" mode). Turning the filter ON in
Advance switches to `tfLuna.inRange(rangeMin, rangeMax)`, the original band-pass logic, completely
unchanged. A shared `renderZoneBar()` helper (fixed 0..`TFLUNA_MAX_DISTANCE_M` scale, so the
configured zone shows in its true physical position rather than scaled to Range Max) is drawn on
both MAIN (always, to give a passive glance at mode/zone) and ADVANCE (for live feedback while
tuning Min/Max) — highlighted zone segment only appears when the filter is enabled.

`AutoShoot::checkAndTrigger()` fires on entry into detection (rising edge) AND keeps re-firing
(cooldown-paced, `AUTOSHOOT_RETRIGGER_DELTA_M` = 0.15m) while the measured distance keeps changing
during a continuous detection — a pure rising-edge design previously meant a long object that
never fully left detection would fire exactly once and never again. `cooldownMs` defaults to 0
(was 500) and its encoder step is 10ms (was 50ms) for finer control near that low default.

Everything else about Auto Shoot's editing UX (encoder step size elsewhere, START/STOP as two
separate buttons rather than a merged toggle, range-edit clamping instead of swapping) was
intentionally kept at its original, hardware-confirmed-stable form — don't casually re-introduce
speed-scaled encoder steps, a TEST button, or similar UX tweaks without hardware verification;
this exact class of change has already had to be reverted once after appearing to destabilize the
sensor.

### Power-on sequence is brownout-sensitive

`FactoryTest::_power_on()` (`src/factory_test/components/ft_key_test.cpp`) latches
`POWER_HOLD_PIN` and, on a manual button press, requires a 2s hold before confirming boot
(otherwise it releases the latch and deep-sleeps). Anything added to `FactoryTest::init()` before
display init runs in this same fragile window — avoid adding current-hungry peripheral
activity there; prefer initializing it lazily inside the mode that actually needs it.

That 2s button hold also has a side effect relied on elsewhere: it gives TF-Luna (same power
rail) real wall-clock time to finish its own physical power-on settling before anything ever
queries it. A boot that skips the hold — VIN2 direct power, or an RTC scheduled wake — doesn't get
that for free, so `main.cpp` passes `bootLogoPlay(canvas, extraHoldMs)` an extra ~2000ms
(added to the logo's already-static hold phase, so it's invisible as a "pause") whenever
`ft._manual_power_on` is false, closing the gap. No TF-Luna/I2C calls involved — pure elapsed
time — so this doesn't carry the brownout/instability risk that touching the sensor at boot did.

### Sleep & Week scheduler runs globally, not just in its own mode

`sleepWeekScheduler.update()` is ticked every frame from `view_update()` (main menu) as well as
inside its own mode loop, so scheduled sleep/wake fires even if the user never opens SLEEP&WAKE.
RTC alarm and actual power-off are wired via function-pointer callbacks
(`SleepWeekScheduler::setAlarmCallback`/`setSleepCallback`) registered once at boot
(`FactoryTest::_scheduler_register_callbacks()`, in `factory_test_sleep_week.cpp`) — the
scheduler class itself has no hardware dependency. On a scheduled RTC wake,
`_scheduler_boot_resume()` auto-enters AUTO_SHOOT or TIMELAPSE (per configured mode) and arms a
5s "AUTO START" countdown popup (`_scheduler_autostart_pending`) consumed inside that mode's own
loop; a manual power-on (`_manual_power_on`) always goes to the main menu instead.

### DisplayMode lives on, but its launcher slot is now MULTI BOX (placeholder)

The old standalone DISPLAY mode's launcher slot is now called MULTI BOX
(`src/factory_test/factory_test_multi_box.cpp`) and is an intentionally empty placeholder
("Coming soon", exits on any press) awaiting design instructions — don't assume it's dead code to
clean up.

The actual `DisplayMode` class, `DisplayModeConfig`, `DisplayPowerSave` engine, and
`display_mode_ui.cpp`'s `renderDisplayUI()` are **completely unchanged** and still do everything
they always did (boot-time apply in `view.cpp`, power-save ticking at the main menu). Only the
*entry point* moved: brightness / power save / theme / rotation are now edited from **SETTING**
mode's "Display" row (`SettingEditMode::Screen::DISPLAY_SETTINGS` — named to avoid colliding with
Arduino.h's `#define DISPLAY 0x1`, which silently breaks enum parsing if reused). `Setting`'s
input handlers forward straight to `displayMode.handleEncoderRotate()`/`handleButtonPress()` when
that screen is active, and `renderSettingUI()` calls `renderDisplayUI()` as-is — so if you need to
change brightness/power-save/theme/rotation behavior, edit `display_mode.cpp`/`display_mode_ui.cpp`
exactly as before; only navigating *to* that screen goes through Setting now.

### UI convention ("GEOPIX UI STANDARD")

All mode UIs share one visual language, documented at the top of
`src/display_mode/display_mode_ui.cpp`: header at y=2 (title top_center, "<" back at x=5),
rounded panel `drawRoundRect(8, 22, 224, H, 5, BORDER)`, item rows 20px tall starting at y=27
with inverted-color fill on selection, an optional bottom status panel at y=113 h=20, no footer
hint captions. Colors are never hardcoded — always the four `UI_FG`/`UI_BG`/`UI_AL`/`UI_BORDER`
globals from `src/common/ui_theme.h` (8 swappable presets) plus fixed semantic
GREEN/RED/YELLOW. New mode UIs should follow this layout rather than inventing a new one.

### BLE camera remote

`CameraDriver` (`src/remote/camera_driver.h`) is an abstract interface implemented per brand in
`src/remote/drivers/{sony,canon,nikon}/`; `RemoteManager` (`src/remote/remote_manager.cpp`)
selects the active brand (persisted in NVS) and routes `triggerPhoto()`/`pairCamera()` to it.
This is exposed to the rest of the firmware as `TriggerMode`'s third channel
(`fireBluetoothIfEnabled()`), fired after the G1/G2 GPIO pulse so BLE latency never affects pulse
timing.

### OTA update

`src/ota_update/` is self-contained: WiFi credentials keyed by CRC32(SSID) in NVS (namespace
`"wifi"`), GitHub Releases JSON parsed with hand-rolled string search (no ArduinoJson), firmware
flashed via `Update.h` to the inactive OTA partition. Any failure path calls `Update.abort()`
before returning, so the currently-running firmware is never left unbootable.

### Legacy code

`src/factory_test/components/*.cpp` and the `ft_*_test.cpp` files (encoder/IO/WiFi/BLE/RTC/key
tests, plus an Arkanoid clone) are leftover hardware bring-up/QA screens from before the current
8-mode launcher existed. They still define some `FactoryTest` methods that current code calls
(`_power_on`, `_disp_init`, `_rtc_init`, `_key_init`), but are otherwise not part of the
user-facing product surface — don't treat them as a model for new mode UI/logic.
