# Adaptive CAD — Site-Calibrated Listen-Before-Talk

ZephCore's listen-before-talk (LBT) runs a hardware Channel Activity
Detection (CAD) before every transmission: the radio's LoRa correlator
searches the channel for chirps, and TX is deferred while activity is
detected. How sensitive that search is comes down to one register value,
`cadDetPeak` — and the right value depends on where the node lives.

- **Too sensitive** (detPeak too low): chirp-like interference — other
  LoRa networks, other spreading factors on the same frequency — triggers
  *false positives*. The node keeps deferring TX for nothing, wasting
  100–200 ms retry cycles and adding latency; in bad cases it hits the
  4-second CAD watchdog.
- **Not sensitive enough** (detPeak too high): real transmissions are
  *missed* and the node transmits over ongoing packets, causing on-air
  collisions.

A quiet valley node and a 50-network hilltop node need different values.
Adaptive CAD measures the site and finds the value, instead of shipping
one hardcoded number to everyone.

Important physics note: `cadDetPeak` is a correlation peak-to-noise
threshold inside the radio's despreader — **not** a dBm level. The RSSI
noise floor cannot be converted into a detPeak value, which is why this
feature measures CAD behaviour directly instead of deriving it from RSSI.

## How it works

Every `cad.probe.interval` seconds (default 60), when the radio is idle
in receive mode, the firmware runs one **calibration CAD probe** and
immediately re-arms RX. A probe takes one CAD duration — about 4 ms at
SF7/250 kHz, up to ~130 ms at SF12/125 kHz — so the added radio deaf time
is roughly 0.1%. Probes also run in RX duty-cycle (sniff) mode: the
duty cycle is briefly interrupted and re-armed, within the same
preamble-catch budget philosophy the sniff-mode math already accepts.

Each probe tests one **level**: a signed offset from the chip family's
per-SF base detPeak (SX126x: `SF+13`; LR11xx/LR2021: the 56–68 table).
Results accumulate per level:

- `probes` — how many CADs ran at this level
- `busy` — raw "activity detected" verdicts
- `fp` — suspected **false positives**: busy verdicts where no packet
  materialised right after (see filtering below)
- `tp` — **true positives**: busy verdicts confirmed by actual RX
  activity immediately after the probe

Filtering, because a "busy" verdict during apparent silence may be a real
below-noise-floor packet (detecting those is CAD's whole purpose):

1. Probes are skipped entirely when the channel is visibly busy (RSSI
   more than 7 dB above the learned noise floor) or a packet is being
   received — those teach nothing about false positives.
2. After a busy verdict, RX is restarted and the firmware waits ~8 symbol
   times: a real transmitter is still on the air and trips the receive
   path. If nothing shows up, the verdict counts as a suspected false
   positive. Residual contamination by real traffic biases the estimate
   *conservative* (higher detPeak), which is the safe direction.

Statistics decay (halve) every 6 hours so the picture stays fresh, and
reset completely whenever the radio parameters change (frequency, SF, BW
— the data is only valid for one configuration).

### Auto mode (staircase)

With `cad.auto on`, a one-sided staircase controller acts on the stats:

- Probes concentrate on the **frontier** — one level more sensitive than
  the current operating point (3 of every 4 probes), with the remainder
  self-checking the operating level.
- **Step down** (more sensitive) when the frontier has ≥300 samples and
  its false-positive rate is ≤1%.
- **Step up** (less sensitive) quickly when the operating level itself
  shows a false-positive rate above 2% over ≥50 samples — false positives
  at the operating point cost real transmissions.
- The offset is clamped to −4…+4 around the family base and persisted to
  flash whenever it steps (steps are hours apart).

Quiet sites converge *below* the standard value — more sensitive LBT than
any fixed-config firmware, meaning fewer TX-over-RX stomps. Noisy sites
ratchet up until false positives vanish.

## Recommended workflow (dry-run first)

Everything ships **observing but not acting**: probes run and counters
accumulate from first boot, but the operating detPeak stays at the
family default until you enable auto mode or set an offset by hand.

1. **Let it observe.** Optionally speed up data collection during the
   observation window:

   ```
   set cad.probe.interval 15
   ```

2. **Read the curve.** After some hours:

   ```
   get cad
   ```

   Example output:

   ```
   > auto:off offset:0 peak:21 (base 21, 4 sym) probe:15s
   lvl -3 (peak 18): 240 probes, 31 busy, 28 fp, 3 tp (fp 11.6%)
   lvl -2 (peak 19): 241 probes, 9 busy, 7 fp, 2 tp (fp 2.9%)
   lvl -1 (peak 20): 240 probes, 3 busy, 1 fp, 2 tp (fp 0.4%)
   lvl +0 (peak 21): 241 probes, 2 busy, 0 fp, 2 tp (fp 0.0%)
   lvl +1 (peak 22): 240 probes, 2 busy, 0 fp, 2 tp (fp 0.0%)
   lvl +2 (peak 23): 240 probes, 1 busy, 0 fp, 1 tp (fp 0.0%)
   ```

   Read it bottom-up: the false-positive rate jumps somewhere (here
   between −1 and −2). The lowest clean level (−1) is your site's knee.
   In dry-run mode the sweep covers levels −3…+2 evenly.

   How long to wait (at the default 60 s interval; divide by 4 at 15 s):

   | After | You get |
   |---|---|
   | 12–24 h | Knee location, coarsely — enough for a manual offset |
   | 2–3 days | Per-level rates at ~1% resolution, incl. day/night variation |
   | 1 week | Solid, weekday/weekend-proof picture |

3. **Act.** Either pin it manually:

   ```
   set cad.offset -1
   ```

   or let the staircase manage it from here on:

   ```
   set cad.auto on
   set cad.probe.interval 60
   ```

   With auto on, `get cad` keeps showing the live state; offset changes
   appear in the log as `cad: step down/up -> offset N` and are saved to
   flash automatically.

## Command reference

| Command | Default | Description |
|---|---|---|
| `get cad` | | Status + per-level statistics (see above). |
| `set cad.auto <on\|off>` | off | Staircase controller acts on the stats. |
| `set cad.offset <n>` | 0 | Operating offset, −4…4. Negative = more sensitive. Applied live. |
| `set cad.probe.interval <sec>` | 60 | Probe cadence; 0 disables probing (and freezes auto), 10–255 otherwise. |
| `set cad.reset` | | Clear accumulated statistics (RAM only). |

All settings persist in prefs and apply to every role — repeater, room
server, and companion (companions reach the CLI via the v-contact admin
chat or USB serial).

## Notes and limits

- **SX127x boards** (TTGO LoRa32, T-Beam classic) have no hardware CAD;
  `get cad` reports `not available` and the settings are inert. Their LBT
  remains the RSSI-based `int.thresh` gate.
- **The false-positive side is measured; the miss side mostly is not.**
  A too-high detPeak shows up as on-air collisions, which a single node
  cannot observe. That is why the workflow is "find the lowest clean
  level and sit at it", never "raise it until problems stop being
  visible". The `tp` column is the one local miss-side signal: probes
  that detected real below-noise-floor traffic.
- **detPeak is the only adaptive knob.** `cadDetMin` stays at Semtech's
  universal 10; the symbol count is fixed at 4 (better mid-payload
  detection than 2 — most of a mesh packet's airtime is payload, and
  that is where a pre-TX CAD usually lands). The drivers scale their
  CAD timeout with symbol count and preset automatically.
- Statistics are RAM-only and restart after a reboot; the learned
  *offset* is persisted. Mixed fleets are fine — this feature changes
  only when *this* node decides the channel is busy, not anything on
  the air.
- Related but separate: `int.thresh` (RSSI-above-floor software gate)
  and `dc.restarts` (sniff-mode false-preamble counter) still work as
  before; adaptive CAD complements them.
