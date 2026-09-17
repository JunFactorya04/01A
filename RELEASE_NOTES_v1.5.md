# Geopix Digital Solutions v1.5

Bulb Mode over Bluetooth now works. Previously it opened the shutter but never reliably
closed it, so long exposures ran past their configured length.

## Bulb Mode over BLE — fixed

Hardware-confirmed on a Sony A7 IV (ILCE-7M4): the shutter now closes exactly when the
bulb countdown reaches 0, and the recorded exposure matches what you configured.

The camera ignores a plain "shutter up" as a closing signal for a bulb exposure — it
closes bulb only on the next full shutter press it receives. The old code sent a
release-only command, which the camera accepted and then did nothing with, so the frame
stayed open until the next shot's press arrived. That press then spent itself closing the
previous exposure instead of starting a new one. Bulb over BLE now sends a complete
closing press.

Note the two trigger paths follow opposite rules on the same camera:

- **Over BLE** — press opens, next full press closes.
- **Over the wired G1/G2 output** — press opens, release closes; the contact stays held
  for the whole exposure.

If you shoot bulb through the wired output, make sure nothing in the signal path only
relays a trigger *pulse*. A PocketWizard Plus II/III, for example, fires a brief contact
closure at the receiver no matter how long its input is held, so bulb cannot pass through
it. A direct cable works, as does any trigger that follows its input contact.

Bulb Mode over BLE currently applies to **Sony**. Canon, Nikon and Fuji use their own
press/release commands and are unchanged in this release.

## Serial logging now works

The firmware never actually opened its serial port, so all diagnostic output it produced
was discarded. Serial output now reaches the USB port used for flashing, which makes
field diagnosis possible. This is also what made the bulb fix above findable.

## Updating

Devices already in the field can update over the air from Setting > OTA Update. For a
blank device, flash `geopix_v1.5_merged_full_flash.bin` at offset 0x0.
