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

The version shown on the Setting > Info screen and the OTA screen's "Current: vX.X" line comes
from `GEOPIX_FW_VERSION` (`src/common/version.h`), which `tools/version.py` (a PlatformIO
`extra_scripts = pre:` hook) stamps at every build from `git describe --tags --always --dirty` —
clean `v1.4` when built exactly at that tag, `v1.4-3-gabc1234[-dirty]` otherwise. **Before cutting
a release, retag so the release commit is exactly what gets built** (`git tag -f -a vX.Y -F
RELEASE_NOTES_vX.Y.md <commit>` then `git push --force origin vX.Y`) — building from a commit
that's merely *near* a tag still stamps the messy `-N-g<hash>` form. This replaced three
independent hardcoded literal version strings (this file's Info screen, `ota_update.h`'s old
`GEOPIX_FW_VERSION "v1.3"`, `factory_test.h`'s `FW_VERISON "v0.1"` on the legacy QA screen) that
had drifted out of sync with each other and with the real release — that's the bug this exists to
prevent, not a hypothetical one.

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
(`forceReleaseBulbIfExposing()`) so the camera's shutter (and, now, a BLE hold — see below) is
never left open indefinitely just because the sequence was interrupted.

**BLE bulb hold — tried, root-caused, currently paused.** After a user report that testing Bulb
Mode over BLE did nothing at all (by original design: `CameraDriver` only exposed one-shot
`trigger()`, and bulb deliberately skipped it rather than fire an uncontrolled second shot on top
of the held G1/G2 exposure), `CameraDriver` gained `shutterPress()`/`shutterRelease()` (implemented
on all four drivers by splitting each one's own `trigger()` press-then-delay-then-release sequence
into its two halves) and `Timelapse` was wired to call them alongside the G1/G2 hold. Real Sony
hardware testing went through several real fixes chasing an intermittent "shutter doesn't close at
the right time" symptom — `shutterRelease()` reconnecting instead of bailing on `!isConnected()`,
a failed press no longer counting as a completed shot, a per-brand `*_RECONNECT_SETTLE_MS` (500ms)
after an actual reconnect, closing via a full `trigger()` press+release instead of a release-only
command (the camera turned out to only close bulb on the *next full press* it receives, not a bare
"up") — each one fixed something real, but the close still wasn't reliable.

**The actual root cause**, found by capturing a live serial log during a hung pairing attempt:
`E BT_APPL: char not added, no resources, see CONFIG_BT_GATTC_MAX_CACHE_CHAR`, reproducible on a
completely fresh `esptool erase_flash` (so not accumulated bond/cache state — a live, per-session
failure). The classic Arduino `BLEDevice` library caches at most `CONFIG_BT_GATTC_MAX_CACHE_CHAR`
= 40 characteristics total, compiled into the precompiled `framework = arduino` core (not
something a normal PlatformIO build here can raise — that would need building the framework itself
from ESP-IDF with a custom sdkconfig). Worse: `BLEClient::getService()` always triggers a full,
*unfiltered* service discovery internally (`BLEClient.cpp`: `esp_ble_gattc_search_service(gattc_if,
conn_id, NULL)` — the filter argument is hardcoded `NULL`, no public API to search just one
service). A camera whose full GATT profile (all services combined — WiFi transfer, GPS, device
info, remote control, etc.) exceeds 40 characteristics overflows this cache *inside the library's
own `connect()` call*, before any of this driver's code — retries, settle delays, auth waits, all
of it — ever gets a chance to run. This is why nothing at the application layer could have fixed
it. A parallel fix (mirroring `CanonBLE`'s existing `c_authDone`/`c_authOk` wait — added to
`SonyBLE::connectTo()` too, since Sony's `getService()` call happened before confirming the
encryption handshake had even finished) is still worth keeping regardless — it's a real
correctness improvement independent of the cache-overflow finding — but did not fix this bug
either, confirming the failure is below the driver's reach entirely.

**RESOLVED — BLE bulb hold works on a Sony A7 IV (ILCE-7M4), hardware-confirmed.** The earlier
"paused, needs a NimBLE port" conclusion was wrong about what was actually blocking it. Two
separate problems were stacked on top of each other, and only one of them was the cache:

1. **The real protocol bug: a bare `SHUTTER_UP` does not close bulb on this camera.** It closes
   bulb only on the NEXT FULL PRESS it receives. The old `shutterRelease()` sent a release-only
   command, which the camera accepted (the write reported OK, the link was up) and then ignored as
   a closing signal -- so the frame ran PAST its configured length and only ended when the *next
   shot's* press arrived, that press spending itself closing the previous exposure instead of
   starting a new one. `SonyBLE::shutterRelease()` now completes the outstanding press
   (`SHUTTER_UP` + `FOCUS_UP`) and then sends a **complete closing press**
   (`FOCUS_DOWN`/`SHUTTER_DOWN`/`SHUTTER_UP`/`FOCUS_UP`). Confirmed on hardware: the shutter now
   closes exactly when the bulb countdown hits 0, and EXIF matches the configured exposure. The
   close takes ~470-500ms (vs ~240ms for the old release-only path) -- that timing difference is a
   useful signal in the log that the new path actually ran.
2. **The GATT cache overflow only bites on RECONNECT.** `E BT_APPL: char not added, no resources`
   still floods the log on every single connect (the A7 IV's full GATT profile far exceeds the
   40-characteristic cache), but it is **not fatal**: Sony's remote-control characteristic gets
   cached before the cache fills, so `connect()` succeeds and commands go through. The failure
   mode is a *reconnect* mid-sequence, where discovery re-runs against an already-full cache. As
   long as the link stays up for the whole exposure it is never hit -- and in testing it never
   dropped once (`[BLE] hold <ms> link=UP` sampled every second across many consecutive 30s
   exposures, zero `link=DOWN`).

**Opposite rules on the two transports, same camera — do not "unify" them.** Over BLE this camera
is a TOGGLE (press opens, next full press closes). Over the wired G1/G2 port it is a HELD CONTACT
(press opens, release closes) and a toggle model was tried there and disproven -- see the wired
bulb section above. Conflating the two sent this investigation down the wrong path more than once.

**NimBLE is therefore no longer required for this feature**, and is downgraded from "the real fix"
to a sensible long-term move: it removes the cache limit entirely, allows discovering just one
service, and uses less flash (the build sits at ~88.5% of the app partition). Revisit it if a
camera ever turns up whose profile pushes the command characteristic itself out of the cache, or
if a mid-sequence reconnect starts failing in the field.

**Canon/Nikon/Fuji `shutterRelease()` deliberately still send a release-only command.** This was a
considered decision, not an oversight -- do not "finish the job" by copying Sony's closing press
into them. Sony is the odd one out: its freemote `SHUTTER_UP` turns out not to end a bulb exposure,
which is why it needs a closing press. The other three each expose an explicit press/release pair
in their own protocol (`CANON_CMD_SHUTTER_DOWN`/`CANON_CMD_NEUTRAL`,
`NIKON_CMD_PRESS`/`NIKON_CMD_RELEASE`, `FUJI_PARAM_PRESS`/`FUJI_PARAM_RELEASE`), which reads as a
hold model where the release command genuinely closes. Adding a closing press on top of a release
that already works would fire an **extra frame on every shot**, or re-open bulb.

Firmware cannot decide this for itself: `writeValue()` returns void, so there is no way to detect
whether a close actually landed -- which is also why the `!ok` fallback in
`TriggerMode::releaseBluetoothShutterIfEnabled()` never fires for these brands (their
`shutterRelease()` returns true whenever the link is up). It needs a real camera.

**When hardware is available**, the test is the same one that caught it on Sony: run a bulb shot
and check whether the exposure ends at the countdown or runs on until the next shot's press. If it
runs on, port Sony's `shutterRelease()` shape to that brand -- complete the outstanding press, then
send a full closing press. None of the three has ever been verified against a real camera.

Wiring: `Timelapse::startBulbExposure()` calls
`TriggerMode::pressBluetoothShutterIfEnabled()` after driving G1/G2 HIGH (so the physical exposure
never waits on the radio) and records the result in `TimelapseState::bulbFiredBLE`; only a
*successful* press is released later, which is what stops a failed press from being counted as a
real exposure. `endBulbExposure()` and `forceReleaseBulbIfExposing()` both release, so STOP/PAUSE
mid-exposure can never leave a shutter open.

**Bulb over a wired link is a HELD contact closure — the link must carry contact *state*, not a
trigger *event*.** `startBulbExposure()` drives G1/G2 HIGH and holds them for the whole exposure;
the camera's wired release port is a plain physical switch, so press opens the shutter and release
closes it. Two things were chased here and both are settled, so don't re-derive them:

1. **A press-to-open / press-again-to-close (toggle) model was tried on hardware and disproven —
   don't re-introduce it.** Sony documents bulb that way for its *BLE* remote protocol, and this
   codebase's BLE work hit exactly that ("only closes bulb on the next full press, not a bare
   shutter up", below). That rule belongs to the BLE command stream, not to the wired contact.
   Driving two 100ms pulses 10s apart produced **two separate short frames**, not one 10s frame —
   each pulse was a complete press-and-release, i.e. its own ~100ms bulb exposure. Two frames out
   is the signature that distinguishes the models, and it says "hold".
2. **A reported "bulb doesn't hold, camera shoots a short frame" turned out to be a PocketWizard
   in the signal path, not a firmware bug.** PocketWizard (Plus II/III) transmits a trigger
   *event*: the receiver closes the camera contact for a brief moment and releases, no matter how
   long the transmitter's input is held. Bulb cannot pass through that link at all — a MultiMAX
   (which holds the receiver contact to follow its input) or a direct wire is required. Everything
   on the box side measured correct throughout: GPIO readback held HIGH for the full 30,000ms, and
   an LED on the trigger output stayed lit the whole time. The LED test only proves the signal
   reaching *whatever is plugged in* — when that's a radio trigger rather than the camera, a
   perfect hold still yields a short frame. **When bulb misbehaves, establish what is between the
   box and the camera body before touching timing/GPIO logic.**

**Serial logging works now — it never did before.** Nothing in this project ever called
`Serial.begin()`, so every `Serial.print`/`printf` scattered through the drivers and modes (all
the BLE driver logging included) silently produced nothing; that is why earlier hardware debugging
had to proceed by guesswork. `src/main.cpp`'s `setup()` now calls `Serial.begin(115200)`, and
`platformio.ini` sets `-DARDUINO_USB_CDC_ON_BOOT=1` so `Serial` routes to the built-in
USB-Serial-JTAG port (the same USB socket used for flashing) instead of UART0 on GPIO43/44, which
this hardware doesn't expose. Note `pio device monitor` needs a real TTY and fails when invoked
non-interactively — drive `pyserial` directly (open the port with `dtr`/`rts` True) to capture
logs from a script. Also: a capture script holding the port makes `pio run --target upload` fail
with "chip stopped responding" — stop the capture before flashing.

**Physical connector labels do not match the code's G1/G2 names.** The box's physically-labelled
G1 is `TRIGGER_G2_PIN` (GPIO2), driven by the row shown as `Trigger` in TRIGGER MODE; the row
shown as `Remote` drives `TRIGGER_G1_PIN` (GPIO1). `hardware_config.h`'s
`Yellow=G2=GPIO2, White=G1=GPIO1` comment describes the connector wiring, not the silkscreen the
user reads. Both lines are parallel trigger outputs (there is no separate focus line), so this is
cosmetic — but it has already caused wrong conclusions twice while debugging.

**Settle Delay** (`TimelapseConfig::bulbSettleSec`, ADVANCE row 4, 0-120s, default 0) survives this
rollback as a standalone feature — extra rest added after Interval, only while Bulb is on,
implemented as its own distinct pause phase (`TimelapseState::isSettling`/`settleStartTime`,
checked in `update()` right after the `isExposing` check and before the Interval check — Expose →
**Settle** (own "SETTLE" status/countdown/progress-bar color) → Rest (Interval, "REST") → Expose
again, not a single merged wait). Its original motivation (BLE reconnect margin) is moot now that
BLE bulb hold is paused, but it's still a generically useful knob (e.g. giving the camera more time
to write a large file before the next shot) so it was left in rather than torn back out.
`perShotMs()`/`solveIntervalMs()` include it in their totals so the MAIN screen's video-duration
calculator stays accurate. ADVANCE is 5 rows now, windowed to 4 visible with scroll indicators,
same `firstVisible` pattern as Setting's MAIN list.

Also worth noting since it read as a bug during the same testing but isn't one: **total time
between shots is Exposure + Interval (+ Settle Delay when set), not just Interval** — this is the
existing, intentional "Interval is REST time after exposure" design described above
(`perShotMs()`), not something this BLE work changed. With Interval shorter than Exposure (e.g.
Interval=6s, Exposure=10s), the real per-shot cycle is necessarily at least the Exposure length
regardless of how Interval is defined — there's no way to rest *during* an open shutter.

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

### DisplayMode lives on; its old launcher slot is now MULTI BOX

The old standalone DISPLAY mode's launcher slot is now MULTI BOX — see the dedicated section below
for what that mode actually does. `DisplayMode`/`DisplayModeConfig`/`DisplayPowerSave` themselves
have nothing to do with Multi Box; they moved to Setting's flat rows (next paragraph), unrelated.

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
`src/remote/drivers/{sony,canon,nikon,fuji}/`; `RemoteManager` (`src/remote/remote_manager.cpp`)
selects the active brand (persisted in NVS) and routes `triggerPhoto()`/`pairCamera()` to it.
This is exposed to the rest of the firmware as `TriggerMode`'s third channel
(`fireBluetoothIfEnabled()`), fired after the G1/G2 GPIO pulse so BLE latency never affects pulse
timing. `CameraBrand` cycling in `TriggerMode::handleButtonPress()` uses `% 5` (None/Sony/Canon/
Nikon/Fuji) — update this modulus if another brand is ever added. Each driver also implements
`shutterPress()`/`shutterRelease()` (press-and-hold primitives, split out of each driver's own
`trigger()` sequence) — currently dormant, not called from anywhere; see the "BLE bulb hold"
paragraph in the Timelapse section above for why (a hard classic-BLEDevice library limit, not
something fixable at this layer) and what it would take to actually use them again.

**Do not switch Sony's `writeValue()` calls to write-without-response** (`false`) — tried once
(all 4 commands in the half-press/full-press/release sequence) to cut BLE ack round-trip latency,
confirmed on real hardware to make the camera **stop receiving the trigger command entirely**.
Reverted back to write-with-response (`true`). If revisiting Sony BLE latency, the shortened
timing constants alone (without touching the write-response mode) were not re-tried in isolation
after this — that remains an open, untested option; changing the response mode is the one
confirmed dead end.

Canon, Nikon, and Fuji were all overhauled/added by comparing against the reference project
github.com/gkoh/furble (a mature multi-brand ESP32 BLE remote using NimBLE; ported to this
project's classic Arduino `BLEDevice` bluedroid client — furble's client-only APIs like
`secureConnection()` don't exist here and had to be approximated). None of the three have been
verified against a real camera yet — all three need real-hardware confirmation before being
trusted, same as any other BLE protocol change in this codebase.

**Canon** (`src/remote/drivers/canon/`): UUIDs and command bytes already matched furble exactly,
so the driver was not rewritten — one real bug was found and fixed instead. `CanonSecurityCB::
onAuthenticationComplete()` set `c_authDone`/`c_authOk` but `connectTo()` never read them, so a
failed or still-in-progress MITM pairing fell through to writing the pairing characteristic and
reporting "paired & saved" anyway. `connectTo()` now blocks (10s timeout) on `c_authDone` and
aborts on `!c_authOk` before touching the pairing characteristic when `doHandshake` is true — this
mirrors furble's explicit `secureConnection()` call (make sure encryption actually succeeded
before proceeding) using the callback-based API classic `BLEDevice` actually exposes.

**Nikon** (`src/remote/drivers/nikon/`) was a full rewrite, not a patch — the previous
EXPERIMENTAL driver had ESP32 as a BLE **server** advertising as "ML-L7" and passively waiting for
the camera to connect, with a standard-SIG-base service UUID and no real pairing handshake at all.
Comparing against furble's `NikonBase`/`NikonRemote` (verified working against a Nikon Coolpix
B600) showed all three of those were wrong: the camera is the BLE server, ESP32 must be the
**client** connecting to it; the real service UUID has a Nikon-specific 128-bit base
(`0000de00-3dd4-4255-8d62-6dc7b9bd5561`, not the SIG base `...-0000-1000-8000-00805f9b34fb`); and
the camera requires a real 4-message handshake (stage 1 we send `{stage,timestamp,id}` -> stage 2
camera echoes an all-zero ack -> stage 3 we send an all-zero message -> stage 4 camera responds
with its serial number) before it will honor any shutter command. Critically, **this handshake is
not a one-time pairing step** — furble re-runs it on every single connection, keyed off a
device/nonce "identity" chosen once at `pair()` time and persisted (NVS key `nikonId`) so every
reconnect's handshake reuses the same identity the camera first accepted. The new driver mirrors
furble's `NikonRemote` variant specifically (there's also a `NikonSmart` variant in furble for a
different pairing mode, not ported). `focus()` is a no-op (returns `ensureConnected()` only) —
the Remote/ML-L7 protocol has no separate half-press command, matching furble.

**Fuji** (`src/remote/drivers/fuji/`) is brand new — no prior driver existed. Fujifilm has two
protocol variants in furble: **Basic** (unsecured, 4-byte pairing token broadcast in the camera's
BLE advertisement manufacturer data, works on older X/GFX firmware) and **Secure** (firmware from
~mid-2025 on, dozens of undocumented characteristics, far more complex). Only **Basic** was
implemented — it fits this project's existing 3-file driver shape (scan by manufacturer data ->
connect -> write token + our name -> get shutter characteristic), while Secure would need
substantially more reverse-engineering than furble's own source documents. If a paired Fuji camera
turns out to be running new-enough firmware to require Secure, `pair()` will simply fail to find a
match (the Basic scan filter checks for the token-bearing manufacturer data and one of two specific
secondary service UUIDs) — there is no fallback or detection message for that case yet.

### MULTI BOX: ESP-NOW multi-node shooting coordinator

`src/multi_box/` — a wireless multi-node system, separate from the BLE camera remote (different
radio stack: native ESP-NOW over WiFi, `esp_now.h` bundled with arduino-esp32, no third-party
library). All boxes run identical firmware; **role** (`MBRole`: NONE/START/FLASH/CENTER) and
**Node ID** are just config, edited in MULTI BOX's own CONNECTION screen. Four abstractions, per
`src/multi_box/*.h`: `MultiBoxProtocol` (the wire format — one packed `MBPacket` struct + a
`MBCommand` enum covering HELLO/HELLO_ACK/READY/START/START_ACK/FLASH_FIRE/END_DETECT/SHOT_DONE/
DISARM/PING/PONG), `NodeManager` (peer persistence + ESP-NOW peer registration + discovery-scan
bookkeeping + per-sender sequence/replay validation), `MultiBoxController` (owns `esp_now_init()`
and the actual recv callback, plus the session state machine), and `FlashTrigger` (pluggable
one-method interface for a FLASH node's physical output — see below).

**Roles and the shooting flow**: a **START** node runs TF-Luna baseline+threshold detection (like
Auto Shoot's zone logic, but reimplemented here rather than shared, since Auto Shoot's own state
must not be touched) and, depending on its own `config.signalMode`, either opens a session
(`EMIT_START`, watches only while no session is active) or closes one (`EMIT_END`, watches only
*while* a session is active — the "finish line"). This is how two START nodes (start-line +
finish-line) stay one role type rather than needing a 4th role — `signalMode` is the actual
distinguishing config, editable per-node in CONNECTION's Signal row (only shown when role ==
START). A **FLASH** node runs the same baseline+threshold logic gated on session-active, and on
crossing calls `flashTrigger.fire()` (currently `StubFlashTrigger` — logs only, no hardware wired)
plus sends `FLASH_FIRE` to CENTER purely for status/logging; this is deliberately independent of
the camera's own flash sync, which stays whatever it already does. **CENTER** is the session
coordinator: on a valid `START` it opens the bulb (mirrors — but is a separate, duplicated
implementation of — Timelapse's own non-blocking bulb-hold pattern, since Timelapse must not be
touched; see `MultiBoxController::openBulb()`/`closeBulb()`), keeps it open until *both*
`config.minBulbSec` has elapsed *and* an `END_DETECT` has arrived (min-bulb-time is a floor, not a
ceiling), broadcasts `SHOT_DONE`, waits `config.rearmMs`, then broadcasts `READY`. A
`config.maxBulbSec` safety cap force-closes the bulb regardless of `END_DETECT` if it never
arrives (lost node/packet) — same "never leave the shutter open on a broken path" principle as
Timelapse's `forceReleaseBulbIfExposing()`, not something the original request spelled out but
consistent with how every other exposure-holding path in this codebase already behaves.

**Anti-replay/staleness**: the ESP-NOW receive callback (`espNowRecvCallback` in
`multi_box_controller.cpp`) only validates packet size and pushes to a FreeRTOS queue — it never
touches GPIO/NVS/UI directly, since it runs off the WiFi driver's own task. `MultiBoxController::
update()` (called from `MultiBox::update()`, every main-loop tick) drains that queue (bounded to
10/tick) and does the real validation: unknown sender MAC → reject; per-sender sequence number
that isn't strictly newer (`NodeManager::acceptSequence()`, wraparound-safe signed-delta compare)
→ reject as duplicate/stale; a queued event older than 3s by the time it's dequeued → dropped
(guards a backlog scenario, not the normal case since the queue drains every ~10ms loop). Clocks
are **not** synchronized across boxes — the packet's `timestamp` field is sender-local `millis()`,
informational only; staleness is enforced from each receiver's own receive-time bookkeeping, not
by comparing timestamps across devices.

**Pairing**: CONNECTION screen's "Pair / Add Node" broadcasts `HELLO`; any box with a role
configured replies `HELLO_ACK` carrying its own node id + self-reported role, so the requesting
box's scan-results list already knows what it found — there's no manual "assign this peer's role"
step, the peer's own role config is trusted as-is. All ESP-NOW traffic is **unencrypted** and
pinned to a hardcoded fixed WiFi channel (`MB_ESPNOW_CHANNEL` = 1 in `multi_box_controller.cpp`)
since none of these boxes ever join a real AP and there's no other authority to agree on a
channel — this was a deliberate v1 simplification (documented, not silently assumed): revisit if
range/interference problems show up, or if the "anyone in radio range can inject session commands"
exposure needs closing (ESP-NOW's own per-peer encryption is available but caps encrypted peers at
6, vs 20 unencrypted — a real hardware limit, not a config knob).

**Never-sleep-mid-shot**: `_multi_box_loop()` (`factory_test_multi_box.cpp`) calls
`DisplayPowerSave::keepAwake()` directly (bypassing the shared `_display_power_save_tick()`
helper, same way the SLEEP/WAKE countdown popup does) whenever `session == EXPOSING`, so the
screen never dims mid-exposure. This only covers the display-dim engine — `SleepWeekScheduler`'s
RTC-driven power-off is a separate, global mechanism (ticked every frame regardless of mode) with
no per-mode veto hook in this codebase; MULTI BOX does not add one, so a scheduled deep-sleep could
in principle still fire mid-session. Flagged here rather than silently patched around, since
building that veto would mean touching shared scheduler code outside this feature's scope.

Long-press on MAIN opens an `EXIT MULTI BOX?` confirm (`CANCEL`/`OK`, defaults to `CANCEL`) instead
of exiting immediately — this reuses the project's existing ~1.5s long-press threshold
(`_read_mode_button_event()`'s default), not a separate 3-second timer. `OK` runs
`MultiBoxController::requestExit()`: force-closes any open bulb, broadcasts `DISARM` to every
paired node, then `esp_now_deinit()` + `WiFi.mode(WIFI_OFF)` before the mode actually exits.
Removing a peer (CONNECTION → Nodes → press) only drops it from the pairing list — it never resets
that peer's own role/config, since it's a different physical box.

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
