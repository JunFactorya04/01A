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

`begin()` now does two more things before `configureFrameRate()`, both scoped entirely inside
`begin()` and never touching `update()`'s detection/polling logic. **Hardware-confirmed**: 20
consecutive full power cycles all booted cleanly straight to 200Hz, where before this the sensor
would intermittently fail to come up or fall back to 100Hz — this is a verified fix, not just a
plausible theory.

1. **`recoverStuckI2CBus()`** — if TF-Luna was mid-transmission when the ESP32 last reset/lost
   power, it can be left holding SDA low forever, waiting for clock pulses a freshly-booted
   master will never send. **No I2C transaction can succeed while this holds — including
   `softResetSensor()`, which is itself an I2C write** — so the self-heal path
   (`begin()` + `softResetSensor()`) could never actually recover a truly stuck bus on its own;
   it would just retry forever. Whether a given boot hits this depends on the exact timing of the
   prior power-down relative to the sensor's I2C cycle, which is why the sensor coming up cleanly
   was inconsistent run to run. Standard fix (same procedure ESPHome's own I2C recovery uses):
   before `Wire1.begin()` ever takes the pins, manually toggle SCL as GPIO up to 20 times to walk
   a stuck slave through releasing SDA, then issue a manual STOP condition. Near-zero cost when
   the bus is already idle (returns immediately on the first check). If SCL itself were ever
   found stuck (not just SDA), this can't fix it — that needs an actual power cycle — and the
   routine doesn't try to special-case that, it just falls through to a `Wire1.begin()` that will
   still fail the same as before this existed.
2. **A bounded, paced ready-wait** (up to 10 attempts, 10ms apart — the sensor's own native frame
   pace, deliberately *not* the old `warmUp()`'s tight busy-loop) between `Wire1.begin()` and
   `configureFrameRate()`. TF-Luna needs real time after power-up to settle before it will answer;
   querying it before that made `configureFrameRate()`'s write/readback fail and permanently miss
   the 200Hz upgrade for that session (the "sometimes falls back to 100Hz" symptom). Hard-capped
   so `begin()` can never hang — a sensor that still isn't answering after 10 tries just falls
   through to `update()`'s existing pacing/backoff/self-heal, exactly as before this existed.

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
(cooldown-paced) while the measured distance keeps changing during a continuous detection — a
pure rising-edge design previously meant a long object that never fully left detection would fire
exactly once and never again. The minimum-movement threshold for that retrigger
(`config.retriggerDeltaCm`, default 15cm) is user-configurable in Advance (4th row, alongside
Range Filter/Min/Max) rather than a hardcoded constant — the right value depends on the
deployment's own sensor noise/reflectivity, not something guessable from code. Applies regardless
of whether the Range Filter is on. `cooldownMs` defaults to 0 (was 500) and its encoder step is
10ms (was 50ms) for finer control near that low default.

Everything else about Auto Shoot's editing UX (encoder step size elsewhere, START/STOP as two
separate buttons rather than a merged toggle, range-edit clamping instead of swapping) was
intentionally kept at its original, hardware-confirmed-stable form — don't casually re-introduce
speed-scaled encoder steps, a TEST button, or similar UX tweaks without hardware verification;
this exact class of change has already had to be reverted once after appearing to destabilize the
sensor.

### Timelapse: video calculator (default) vs. the Advance direct-entry screen

Same MAIN/ADVANCE split as Auto Shoot and Trigger Mode, built around the observation that most
timelapse shooting is done to make a video, so the primary screen speaks in those terms rather
than raw interval math.

`intervalMs`/`totalShots` in `TimelapseConfig` remain the **only persisted, actually-used**
shooting parameters. **MAIN** shows Shoot Duration, Video Length, and Video FPS (24/25/30) as
editable fields, but these are *derived views* over interval/totalShots, not separate storage —
editing one solves back into `intervalMs`/`totalShots`. The rule that keeps this well-defined
regardless of which field you approach it from: **whichever field you're turning right now is
held fixed at its current value; the other two are recomputed from it** (see
`Timelapse::handleEncoderRotate()`'s MAIN-screen switch for the three cases). `solveIntervalMs()`
does the `durationSec * 1000 / shots` math in `long long` and clamps before narrowing to `int` —
a real overflow was caught here in review (durationSec can reach ~36,000,000 at maxed-out
Interval/Total Shots; ESP32's `long` is 32-bit and `*1000` of that overflows it before
`validateConfig()` would otherwise have clamped the result).

**ADVANCE** holds direct Interval/Total Shots entry (unchanged logic, just relocated) plus **Bulb
Mode** — milky way / astro long exposures. When `bulbEnabled`, each shot **holds** G1/G2 for
`bulbExposureSec` (1-900s) instead of the usual ~6ms pulse. This is a **non-blocking state
machine** (`state.isExposing`, checked every `update()` tick), deliberately not a blocking
`delay()` — a 10-30s+ blocking hold would freeze input handling and rendering for the whole
exposure, unlike the existing brief non-bulb pulse. **Interval means REST time strictly AFTER an
exposure completes when Bulb is on** — `lastShotTime` is set in `endBulbExposure()` (completion),
not `startBulbExposure()` (start), so exposure and rest are sequential, never overlapping. An
earlier version measured Interval from shot-START to shot-START and raised its floor to the
exposure length, which left **zero** real rest time whenever Interval sat at that floor — the
camera had no time to write/process a long exposure before the next one fired. Total per-shot
cycle time (`Interval + Exposure` when Bulb is on) is computed by `perShotMs()`, used by
`getEstimatedDurationSec()`/`currentDurationSecOrBootstrap()`/`solveIntervalMs()` so the MAIN
screen's calculator accounts for exposure time rather than understating the real duration.
`stop()`/`pause()` force-release a mid-exposure hold
(`forceReleaseBulbIfExposing()`) so the camera's shutter is never left open indefinitely just
because the sequence was interrupted. The BLE camera-remote channel has no held/bulb command
(`CameraDriver` only exposes one-shot `trigger()`) and is **skipped entirely** for the whole bulb
sequence (`endBulbExposure()` deliberately does not call `fireBluetoothIfEnabled()`) — an earlier
version fired it anyway after the hold released, which landed as a second, uncontrolled shot
right on top of the held exposure (two trigger paths firing "in parallel" on the same sequence).
Bulb mode is exclusive to the physical G1/G2 hold.

The status box's realtime countdown ("next Ns" / "Bulb Ns") + fill progress bar finally puts the
`getTimeUntilNextShot()` getter to use — it existed in the original code but was never rendered
anywhere before this.

**Bootstrap-from-infinite gotcha**: editing Video Length or Video FPS on MAIN while
`totalShots == 0` (infinite, the default) must NOT solve a new Interval the normal way. The
normal path bootstraps "current duration" from a degenerate one-shot assumption
(`perShotMs() * 1`), and solving Interval to stretch that tiny duration across the much larger
shot count FPS/VideoLength implies produces absurdly short intervals (default 5s Interval treated
as "1 shot", stretched to fit a 1s@30fps video's 30 shots -> ~166ms Interval — this was a real
reported bug, "fires 1 shot per second" with no real spacing). The fix: when `totalShots` was 0
at the start of the edit, leave `intervalMs` untouched and just set `totalShots` directly;
Duration then follows naturally instead of being derived from an ungrounded baseline.

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

The actual `DisplayMode` class, `DisplayModeConfig`, and `DisplayPowerSave` engine are
**completely unchanged** and still do everything they always did (boot-time apply in `view.cpp`,
power-save ticking at the main menu) — `display_mode_ui.cpp`'s `renderDisplayUI()` itself is no
longer called from anywhere, though, since the fields it drew are now rendered as part of
Setting's own list (see below). Only the *entry point and rendering* moved, not the underlying
config/apply/persistence logic.

Brightness / Power Save / Theme / Rotation are edited as **flat rows directly in SETTING mode's
MAIN list** (indices 2-5, between Speaker and Info) — deliberately NOT a "Display >" sub-screen;
that was tried and reverted per feedback. Setting's MAIN screen is 7 rows total and doesn't fit
the display at once, so `setting_ui.cpp` windows it to 4 visible rows with scroll indicators,
reusing the same `firstVisible` windowing pattern the OTA update mode's WiFi/release lists already
use (`factory_test_ota_update.cpp`) rather than inventing a new one.

Setting owns navigation/selection for these rows itself (`SettingEditMode` has no separate screen
for them). Only the **per-field value stepping** — brightness ±10, the power-save preset table,
theme wrap, rotation toggle — is delegated to `DisplayMode`'s existing `handleEncoderRotate()`,
by temporarily pointing `displayMode.editMode.selectedIndex`/`state` at the right field
(Setting's index minus 2) right before calling it. This avoids duplicating that stepping logic in
two places where it could drift out of sync. Persistence (`displayMode.saveConfig()` +
re-apply) happens once, unconditionally, at the top of `Setting::handleButtonLongPress()` — that
function is always effectively "exit Setting" since
`FactoryTest::handleSettingButtonLongPress()` unconditionally exits on any long press (pre-existing
behavior, not something introduced by this change) — matching `DisplayMode`'s original
"save on exit" rule from when it was its own standalone mode.

### Display power save runs in every mode, via one shared helper

`DisplayPowerSave` (in `display_mode.cpp`) used to only tick inside `view.cpp`'s launcher —
entering any mode called `exitPowerSave()` and nothing re-armed the dim/screen-off timer, so the
screen stayed at full brightness indefinitely while idle inside any mode. It's now wired into
every mode's loop (Auto Shoot, Timelapse, Trigger Mode, Sleep & Week, Setting, Multi Box) through
one shared method, `FactoryTest::_display_power_save_tick()` (`factory_test_input.cpp`) — call it
once per loop iteration right before that mode's own `handleXxxInput()`; a `true` return means
this cycle's button/encoder movement was just consumed to wake the screen back up, so skip the
real `handleXxxInput()` call for that one cycle (mirrors the swallow-on-wake pattern already used
in the launcher, so a "waking tap" can't also fire a real command like toggling STOP). This is
purely `_disp->setBrightness()` on the backlight — it never gates or delays a mode's actual
functional logic (TF-Luna polling, timers, trigger GPIO), which keeps running every cycle
regardless. `DisplayPowerSave::keepAwake()` forces full brightness and resets the idle timer
without a keypress; both the shared helper and the launcher's own tick call it whenever
`schedulerPopupActive()` (or `_scheduler_autostart_pending`) is true, since a SLEEP/WAKE/AUTO
START countdown exists specifically to alert a person and must never dim out from under them.

OTA Update is deliberately **not** wired into this — its screen is one large state-machine
`switch` (WiFi scan, password entry, flashing) rather than the simple loop+`handleInput` shape
every other mode has, and its flow is normally actively supervised, so it needs its own
integration pass rather than reusing this helper blindly.

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

**Do not switch Sony's `writeValue()` calls to write-without-response** (`false`) — tried once
(all 4 commands in the half-press/full-press/release sequence) to cut BLE ack round-trip latency,
confirmed on real hardware to make the camera **stop receiving the trigger command entirely**.
Reverted back to write-with-response (`true`). If revisiting Sony BLE latency, the shortened
timing constants alone (without touching the write-response mode) were not re-tried in isolation
after this — that remains an open, untested option; changing the response mode is the one
confirmed dead end.

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
