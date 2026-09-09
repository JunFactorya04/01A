# Geopix Firmware v1.4

Automatic wildlife/traffic camera trigger box firmware — ESP32-S3, LovyanGFX display,
rotary encoder UI, Benewake TF-Luna LiDAR, dual GPIO camera trigger (G1/G2) + optional
BLE camera remote + optional ESP-NOW multi-box coordination.

## Modes

- **AUTO SHOOT** — TF-Luna based motion/proximity trigger. Default "pure" mode fires on
  any valid sensor return; an Advance screen adds an optional min/max range band-pass
  filter and a user-configurable retrigger distance (keeps firing while a long object
  stays in view, instead of only once on entry). Live zone bar, 200Hz sensor pacing.
- **TIMELAPSE** — MAIN screen is a video calculator (Shoot Duration / Video Length /
  FPS) that solves back into interval/shot-count; Advance screen has direct
  Interval/Total Shots entry plus **Bulb Mode** for milky-way/astro long exposures
  (non-blocking hold, correct rest-time-after-exposure sequencing, safety release on
  stop/pause).
- **TRIGGER MODE** — dual GPIO output (G2 main / G1 backup) plus a third channel: BLE
  camera remote (Sony / Canon / Nikon / Fuji drivers).
- **SLEEP & WEEK** — RTC-scheduled sleep/wake, with an auto-start countdown into Auto
  Shoot or Timelapse on a scheduled wake.
- **SETTING** — date/time, speaker on/off, brightness, power-save timeout, theme (8
  presets), display rotation.
- **MULTI BOX** *(new in v1.4)* — wireless multi-node coordination over native ESP-NOW.
  Any number of boxes can be assigned START, FLASH, or CENTER roles to run a
  synchronized bulb-exposure shooting flow (start-line/finish-line detection opens and
  closes one CENTER box's bulb exposure; FLASH nodes fire an independent illumination
  trigger mid-exposure). See CLAUDE.md for the full protocol/state-machine writeup.
- **OTA UPDATE** — WiFi-based firmware update from GitHub Releases, flashed to the
  inactive OTA partition; any failure path aborts cleanly, current firmware is never
  left unbootable.

## Hardware reliability

- TF-Luna I2C: stuck-bus recovery (manual SCL toggle + STOP condition) runs before
  every `begin()`, plus a bounded paced ready-wait before raising the sample rate to
  200Hz (falls back to 100Hz if the sensor doesn't confirm). Verified across 20
  consecutive full power cycles with no missed boot.
- Boot sequencing hardened against brownout on weak batteries (no sensor I/O before
  display init proves the supply rail is stable).

## BLE camera remote

- **Sony** — working (write-with-response; write-without-response was tried once to
  cut latency and confirmed on hardware to break triggering entirely — do not retry).
- **Canon** — fixed a real bug: a failed/incomplete pairing authentication was never
  checked, so it could report "paired" without the camera actually accepting it.
- **Nikon** — full rewrite: corrected BLE role (ESP32 is the client, not a server),
  corrected service UUID, added the real 4-message pairing handshake the camera
  requires.
- **Fuji** — new driver (Basic/unsecured pairing protocol).

## Known limitations

- Canon, Nikon, and Fuji BLE drivers have not yet been verified against real cameras —
  logic is ported from a working reference implementation but untested on this
  hardware.
- MULTI BOX: ESP-NOW traffic is unencrypted on a fixed channel (v1 simplification); no
  hook yet prevents the Sleep & Week scheduler's RTC deep-sleep from firing mid-shot.
