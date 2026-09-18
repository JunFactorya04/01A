# Geopix Digital Solutions v1.7

A new **FOR DEVELOP** mode lets MULTI BOX be set up and tested with a single box, and six
faults it uncovered in MULTI BOX are fixed.

## FOR DEVELOP — test MULTI BOX with one box

MULTI BOX normally needs two boxes: one at the start line to open the shutter, one at the
camera to close it. This mode stands in for the start-line box with a timer, so the camera
box can be set up, aimed and tested on its own.

Everything else behaves exactly as it does in a real multi-box shoot, so what you tune here
is what you get in the field.

- **Shoot SINGLE or AUTO** — one complete frame per press, or keep cycling.
- **Live distance, baseline and a cycle bar** showing which phase the shot is in.
- Settings grouped into **Advance**, **Sensor** and **Trigger Out** pages.

Found on the ninth launcher card. POWER OFF is unchanged and still where it was.

## MULTI BOX — six fixes

These were all found using the mode above. Each one would have cost a session in the field:

- **The camera box never watched for the subject finishing.** Its sensor was never started,
  so every frame ran to the maximum exposure instead of ending when the subject arrived —
  indistinguishable from a broken sensor.
- **A box with no role assigned could not pair with anything.** Scanning found nothing, no
  matter how long you waited. This is the first thing anyone does with a new box.
- **Node lists showed only the first four entries** while you could still scroll to and
  delete ones you could not see.
- **The low-battery warning only worked while the shutter was open.**
- **The bulb timer kept counting after STOP.**
- **Minimum exposure time now truly ignores the sensor** for its duration, instead of
  remembering an early reading and ending the frame the moment the minimum expired.

## MULTI BOX — new controls

- **End Delay** (0–10s) — wait after the subject is detected before closing the shutter. The
  sensor fires as they *enter* the beam, but a runner still has to clear the frame.
- **Range Filter** — ignore anything outside a distance band, so distant background cannot
  affect detection. Available on every role; previously it existed internally with no way to
  change it.
- The sensor's detection settings are available on **every** role, not only the camera box.

## Updating

Devices already in the field can update over the air from Setting > OTA Update. For a blank
device, flash `geopix_v1.7_merged_full_flash.bin` at offset 0x0.

**Note**: the radio link between two boxes is still unverified on hardware — pairing,
heartbeat and the start/finish handshake have only been exercised with one box. Every other
mode is unaffected.
