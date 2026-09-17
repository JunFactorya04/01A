# Geopix Digital Solutions v1.6

Battery is now reported as a percentage everywhere, with a low-battery warning and a
safe automatic shutdown. MULTI BOX has been reworked around bulb shooting.

## Battery

The launcher used to show a raw voltage, and no other screen showed anything at all —
a multi-hour timelapse gave no indication of how much power was left once you entered
the mode.

- **Percentage instead of volts**, shown in the header of **every mode**.
- **Low-battery warning** at ~3.5V: the figure turns red and the device chirps once. It
  never interrupts a shoot in progress.
- **Safe shutdown** at ~3.3V: the running sequence is stopped cleanly — closing an open
  bulb exposure and saving settings — before the device powers off, rather than cutting
  out mid-frame.

Both thresholds require the reading to stay low **continuously** (10 seconds to warn, 60
to shut down). Firing a trigger, the Bluetooth radio transmitting and the distance sensor
all dip the voltage briefly, and none of that should be mistaken for a flat battery.

Charging status is not shown. The hardware exposes only a voltage, and a charging pack
and a full one read the same, so there is nothing reliable to display.

## MULTI BOX

This mode now shoots bulb exclusively, with the shutter held open between two sensor
crossings instead of by a timer:

```
START box sees the subject   ->  MAIN box opens the shutter
                                 subject moves, FLASH box fires mid-way
                                 MAIN's own sensor sees the subject
                                 ->  shutter closes, cycle complete
```

- **CENTER is now called MAIN.** Existing boxes keep their assigned role.
- **MAIN closes the shutter itself** using its own sensor, so the finish line is the
  MAIN box. It no longer needs a second box acting as one.
- **Each role has its own screen and its own settings**, shown automatically once the
  role is assigned — all boxes still run the same firmware.
- **FLASH fires for real** (it previously did nothing), with an adjustable delay so the
  light lands where the subject will be rather than where it was first seen.
- **Bulb works over Bluetooth here too**, not only over the trigger cable.
- **Shooting settings are adjustable at last** — detection distance, minimum and maximum
  exposure, and the rest between shots previously ran at fixed values with no way to
  change them.
- **Setup mistakes are now reported** instead of failing quietly: duplicate node IDs, two
  MAIN boxes, or two START boxes each raise an on-screen warning.
- Changing something that would disturb a run in progress asks for confirmation first.

Several faults that could stall the system have been fixed, including roles not
propagating unless boxes were paired in a particular order, a disconnected box still
showing as linked, and a box staying locked for up to a quarter of an hour if the MAIN
box lost power.

**Not yet verified on hardware**: the multi-box radio cycle needs two boxes to exercise,
and has only been checked on one. Treat MULTI BOX as untested in this release. Every
other mode is unaffected.

## Updating

Devices already in the field can update over the air from Setting > OTA Update. For a
blank device, flash `geopix_v1.6_merged_full_flash.bin` at offset 0x0.
