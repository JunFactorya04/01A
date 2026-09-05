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
(bus 0). Polling is paced to the sensor's 100Hz frame rate (10ms) with backoff while failing, and
has self-healing (bus reinit + sensor soft-reset) after `TFLUNA_MAX_FAILS` consecutive failures.
**`sleep()`/`wake()` (register 0x25) exist in the API but must never be called on mode
entry/exit** — field testing showed writing that register can stall the sensor for many seconds;
the comment in the source explains this. `warmUp()` (bounded burst of reads to pre-settle the
sensor) is intentionally **unused/dead code** — `AutoShoot::init()` uses plain `tfLuna.begin()`.
It was tried both at boot (`FactoryTest::init()`, right after the power latch) and inside
`AutoShoot::init()`; either placement made the sensor/board noticeably less stable on real
hardware (the boot placement also risked brownout on a weak battery — see below). If revisiting
sensor warm-up, treat it as a real regression risk, not a free win, and verify on hardware before
keeping it.

Auto Shoot's trigger logic (`AutoShoot::checkAndTrigger()`) fires on entry into the configured
range AND continues re-firing (cooldown-paced) while the measured distance keeps changing inside
the zone — a pure rising-edge design previously meant a long object that never fully left the
zone would fire exactly once and never again. This is the one deliberate behavior change kept on
top of the original Auto Shoot logic; everything else in `auto_shoot.cpp`/`auto_shoot_ui.cpp`
(encoder step size, START/STOP buttons, range edit clamping, etc.) was intentionally reverted to
its original form after other experimental tweaks proved less stable — treat the current
`checkAndTrigger()` + `lastTriggerDistance` field as the stable baseline, not as one option among
several to keep iterating on casually.

### Power-on sequence is brownout-sensitive

`FactoryTest::_power_on()` (`src/factory_test/components/ft_key_test.cpp`) latches
`POWER_HOLD_PIN` and, on a manual button press, requires a 2s hold before confirming boot
(otherwise it releases the latch and deep-sleeps). Anything added to `FactoryTest::init()` before
display init runs in this same fragile window — avoid adding current-hungry peripheral
activity there; prefer initializing it lazily inside the mode that actually needs it.

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
