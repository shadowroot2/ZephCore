# ThinkNode M3-MAMA

M3-MAMA is a personal local profile of `thinknode_m3`, not a copied board. Every
hardware fix and common behaviour change made for ThinkNode M3 must therefore
also apply to M3-MAMA.

Do not include this profile in public release matrices, firmware assets or
release announcements. Build it separately only at the user's request.

Allowed differences are recorded only in `mama.conf` and `mama.overlay`.
Current difference:

- one short press: Tracking after the SOS hold window;
- one short press followed by a hold: SOS;
- two short presses: Advert;
- three short presses: buzzer mode;
- four short presses: LEDs;
- five short presses: GPS.

The single-tap delayed action and the two-tap action are swapped relative to M3.
The SOS gesture and the Tracking/Advert feedback sounds are unchanged.

Build locally with:

```sh
scripts/shadow_build.sh thinknode_m3_mama companion
```
