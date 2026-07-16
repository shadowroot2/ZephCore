# Adaptive Power Control (APC) for ZephCore

## Context

TX power is a static user setting (`NodePrefs.tx_power_dbm`, default 22 dBm). When neighbors are nearby and receiving with 20+ dB of excess SNR margin, we're wasting battery and adding unnecessary channel energy. APC automatically reduces TX power when echo packets (dupes of our own transmissions, heard back from neighbors who retransmitted them) indicate strong link margins, and ramps back up when data goes stale (neighbor offline/moving).

This is a novel "echo-based" approach — no published LoRa APC uses this technique. It's well-suited to flood mesh because every retransmit naturally produces echoes without any protocol overhead.

## Status

**Implemented and building** on all boards. **Compiled in by default** (`CONFIG_ZEPHCORE_APC=y`) but **disabled at runtime** — enable per-node with `set tx apc` (persisted in prefs, survives reboot). Works for both companion and repeater roles.

## Important: Link Asymmetry

APC measures the **return path** SNR (neighbor → us), not our outgoing SNR (us → neighbor). These differ when nodes have mismatched hardware — especially nodes with poor RX sensitivity ("bad ears").

**Path loss is reciprocal** (same frequency, same physical path), so echo SNR is a good proxy for link quality in most cases. The target margin provides a safety buffer for hardware asymmetry.

**If your network has nodes with poor RX hardware**, increase the target margin:
- Default: 16 dB (good for networks with similar hardware)
- 20-22 dB: recommended for mixed hardware networks
- 24-30 dB: very conservative, for networks with known bad receivers

See [CLI Commands](#cli-commands) for how to change the margin at runtime.

## Architecture

### Class: `mesh::PowerController`

Follows the `ContentionTracker` pattern: static ring buffer, EMA, `tick()` from maintenance loop.

**File:** `include/mesh/PowerController.h`, `src/PowerController.cpp`

```
PowerController
  _ring[16]            <- tracks recently sent packets (FNV-1a hash)
  _margin_ema_x256     <- EMA of link margin (SNR - SF_threshold), fixed-point
  _power_reduction_db  <- current TX power reduction (0 to MAX_REDUCTION)
  _target_margin_x4    <- configurable target margin (default 64 = 16 dB)
  _enabled             <- runtime enable/disable (object defaults true; each role
                          applies prefs.apc_enabled at begin() — prefs default 0 = off)
  _last_echo_ms        <- timestamp of most recent echo (for staleness)
  _sf                  <- current spreading factor (for threshold lookup)
```

**Constants:**
| Parameter | Value | Rationale |
|-----------|-------|-----------|
| RING_SIZE | 16 | Match ContentionTracker; handles ~3 pkts/s with 5s window |
| ECHO_WINDOW_MS | 10,000 ms | 10s: covers SF12 2-hop echoes (~7s airtime + processing) |
| STALE_MS | 120,000 ms (2 min) | Mobile mesh — neighbors move/die fast |
| EMA_SHIFT | 2 (alpha=1/4) | More responsive than contention's 1/8 |
| WARMUP_COUNT | 3 | Need a few echoes before acting |
| MAX_SOURCES | 3 | Track up to 3 distinct first-hop echo sources per packet |
| STEP_DOWN_DB | 3 | ~halving power per step, conservative |
| STEP_UP_DB | 6 | Aggressive recovery when margin drops |
| MAX_REDUCTION_DB | 12 | Floor at 10 dBm (from 22 max) |
| CLUSTER_WIDTH_X4 | 24 | 6 dB in x4 units — echo SNRs within 6 dB of best are clustered |
| DEFAULT_TARGET_MARGIN_X4 | 64 | 16 dB above SF sensitivity (configurable at runtime) |
| HYSTERESIS_X4 | 4 | 1 dB (x4 units). Reduce at margin > target+1, increase at margin < target-1 |
| MIN_TX_POWER_DBM | -9 | SX1262 hardware minimum |

**Public API:**
- `setEnabled(bool en)` / `isEnabled()` — runtime enable/disable
- `setSF(uint8_t sf)` — set current SF for margin calculation
- `setTargetMargin(uint8_t margin_db)` / `getTargetMargin()` — configure target link margin (default 16 dB)
- `trackTransmit(uint32_t hash32, uint32_t now_ms)` — called when we send or retransmit a flood
- `recordEcho(uint32_t hash32, int8_t snr_x4, uint8_t first_hop_hash, uint32_t now_ms)` — called on flood dupe; updates best SNR and source diversity; returns true if matched
- `tick(uint32_t now_ms)` — finalize expired entries into EMA, adjust power, handle staleness
- `getPowerReduction() const` -> `int8_t` (0 to MAX_REDUCTION_DB; returns 0 when disabled)
- `getMarginEstimate() const` -> `float` (dB, for diagnostics)
- `getLastSourceCount() const` -> `uint8_t` (echo source count from most recent entry, for diagnostics)
- `isWarmedUp() const` / `isStale(uint32_t now_ms) const`

**Per-source SNR tracking in EchoEntry:**
```cpp
struct EchoEntry {
    uint32_t hash32;
    uint32_t timestamp_ms;
    uint8_t  source_count;
    uint8_t  sf_at_track;    /* SF when packet was transmitted */
    Source sources[MAX_SOURCES];
    bool     active;
};
```

Each entry stores the SF at track time (`sf_at_track`) so that margin calculation uses the correct threshold even if the radio SF changes while entries are in-flight.

**`recordEcho` logic:**
1. Find matching entry by hash32
2. Check if entry has expired (beyond ECHO_WINDOW_MS) — if so, finalize and reject
3. Check if `first_hop_hash` is already in `sources[]`:
   - If yes: update its SNR if the new one is better
   - If no: add new source with its SNR, increment `source_count`
4. Update `_last_echo_ms`

**Computing "robust SNR" when finalizing an entry:**
1. **0 sources** (no echo): return SF threshold (margin = 0, conservative)
2. **1 source**: use its SNR directly (no rogue detection possible, and no need)
3. **2-3 sources**: sort descending, cluster within CLUSTER_WIDTH (6 dB) of the best:
   - If 2+ in cluster -> median the cluster values (2: average, 3: middle)
   - If only 1 in cluster (top value is isolated = rogue) -> drop it, use next source(s)

**Power adjustment algorithm (in `tick()`):**
1. Finalize expired entries: `margin = robust_snr_x4 - sfThresholdX4(sf_at_track)`
2. Entries with **no echo heard**: count as margin=0 (conservative — assume the worst)
3. Feed margin into EMA (x256 fixed-point, warmup seeding for first 3 entries)
4. **Staleness takes priority** (mutually exclusive with margin-based adjustment):
   - If `now - _last_echo_ms > STALE_MS (2 min)` -> ramp reduction toward 0 by STEP_DOWN_DB per tick
   - When stale, **never increase reduction** — old EMA data is unreliable
5. Otherwise compare margin_ema vs target:
   - margin > target + HYSTERESIS -> reduce by STEP_DOWN (3 dB), capped at MAX_REDUCTION_DB
   - margin < target - HYSTERESIS -> increase by STEP_UP (6 dB)
   - in between -> hold

### Integration points (all guarded by `#ifdef CONFIG_ZEPHCORE_APC`)

**1. Track originated packets** — `src/Mesh.cpp` `sendFlood()` (both overloads)

After `_tables->hasSeen(packet)`:
```cpp
uint32_t h = ContentionTracker::computePacketHash32(packet);
_power_ctrl.trackTransmit(h, (uint32_t)_ms->getMillis());
```

**2. Track retransmitted packets** — `src/Mesh.cpp` `routeRecvPacket()`

Alongside existing `_contention.trackRetransmit()`:
```cpp
_power_ctrl.trackTransmit(h, (uint32_t)_ms->getMillis());
```

**3. Record echoes** — `src/Mesh.cpp` `onRecvPacket()`

In the flood dupe detection block, extracting first path hash for diversity:
```cpp
uint8_t first_hop = (pkt->getPathHashCount() > 0) ? pkt->path[0] : 0;
_power_ctrl.recordEcho(h, pkt->_snr, first_hop, (uint32_t)_ms->getMillis());
```

**4. Tick + propagate to radio** — `src/Mesh.cpp` `maintenanceLoop()`

```cpp
_power_ctrl.tick(now);
_radio->setTxPowerReduction(_power_ctrl.getPowerReduction());
```

**5. Apply power reduction** — `adapters/radio/LoRaRadioBase.cpp` `buildModemConfig()`

After existing TX power clamps:
```cpp
cfg.tx_power -= _tx_power_reduction_db;
if (cfg.tx_power < -9) cfg.tx_power = -9;
```

The config cache (`configParamsEqual`) already compares `tx_power`, so a changed reduction naturally triggers `hwConfigure()` on next TX — no explicit reconfigure needed.

**6. Set SF** — `RepeaterMesh::begin()` and `CompanionMesh::begin()` / BLE radio param change callbacks.

### Radio interface

Virtual APC methods added to `mesh::Radio` (base class):
```cpp
virtual void setTxPowerReduction(int8_t reduction_db) { (void)reduction_db; }
virtual int8_t getTxPowerReduction() const { return 0; }
```

`LoRaRadioBase` overrides these with a stored `_tx_power_reduction_db` member.

### Kconfig

In `Kconfig` under "LoRa Power Saving" menu:

```kconfig
config ZEPHCORE_APC
    bool "Adaptive Power Control (APC)"
    default y
```

**Compiled in by default, disabled at runtime.** The `apc_enabled` pref (default 0)
gates it per-node; `set tx apc` enables and persists. Build with
`-DCONFIG_ZEPHCORE_APC=n` to exclude the code entirely (zero overhead). Works for
both companion and repeater roles.

### CLI commands

**`get tx`** — shows current APC state:
```
> 16dBm (apc=on max=22 reduction=6 margin=18.5 target=16)   # APC enabled
> 22dBm (apc=off)                                            # APC disabled
```

**`get tx apc`** — same data in a diagnostics-first form:
```
> apc=on effective=16dBm max=22 reduction=6 margin=18.5 target=16
> apc=off max=22dBm target=16
```

**`get apc.margin`** — shows current target margin:
```
> 16 dB
```

**`set tx apc`** — re-enables APC (default state). APC resumes with existing EMA data.
```
OK - tx power=22 dBm (apc=on)
```

**`set tx <number>`** — disables APC and sets fixed TX power:
```
OK - tx power=16 dBm (apc=off)
```

**`set apc.margin <dB>`** — set APC target link margin (range 6-30 dB):
```
OK - APC target margin=20 dB
```

The user's TX power setting (`NodePrefs.tx_power_dbm`) is always the ceiling — APC only subtracts from it. When APC is disabled, `getPowerReduction()` returns 0 but internal tracking continues, so re-enabling is seamless.

**Note:** Both the APC enable state (`apc_enabled`) and the target margin (`apc_margin`) are persisted in prefs and survive reboots.

### Target margin — what it means and how to choose

The target margin controls how much "extra" signal strength APC tries to maintain above the minimum required for reliable reception at the current SF.

**How it works (SF8 example, threshold = -10 dB):**

| Target margin | Reduce power when SNR > | Increase power when SNR < | Notes |
|--------------|------------------------|--------------------------|-------|
| 16 dB (default) | +7 dB | +5 dB | Good for networks with similar hardware |
| 20 dB | +11 dB | +9 dB | More conservative, safer with mixed hardware |
| 24 dB | +15 dB | +13 dB | Very conservative, for bad-ear networks |
| 10 dB | +1 dB | -1 dB | Aggressive, maximum power savings |

**The formula:**
- Reduce threshold = SF_threshold + target_margin + 1 dB hysteresis
- Increase threshold = SF_threshold + target_margin - 1 dB hysteresis

**Example scenarios:**

**Scenario 1: Two good radios on a rooftop, 500m apart**
Echo SNR = +15 dB. With default margin (16 dB), target SNR for SF8 = +6 dB.
Margin = 15 - (-10) = 25 dB. 25 > 17 -> APC reduces power.
After several ticks: reduction = 9 dB. Effective TX = 13 dBm.
Echo SNR drops to ~+6 dB. Margin = 16 dB. In the hysteresis band -> hold.

**Scenario 2: Good radio talking to a cheap node with -5 dB RX degradation**
You hear the echo at +15 dB, but the cheap node only hears you at +10 dB.
With default margin (16 dB): APC reduces to margin ~16. The cheap node sees ~11 dB.
Still above threshold (-10) by 21 dB. Safe.
With `set apc.margin 10`: APC reduces to margin ~10. Cheap node sees ~5 dB.
Only 15 dB above threshold. Might be marginal in fading conditions.

**Scenario 3: Your network has radios with 10+ dB RX variation**
Some nodes have external LNAs (+3 dB), others have bad antennas (-7 dB).
Total asymmetry up to 10 dB. Set `set apc.margin 22` to ensure the worst
receiver still gets 12 dB of real margin after APC reduces power.

**Rule of thumb:**
- Default (16 dB): most networks
- Add the worst-case RX asymmetry in your network to 16 dB
- If you don't know: 20 dB is a safe middle ground

## What APC does NOT see

- **Zero-hop packets** (advertisements): these are not retransmitted, so no echo is produced. APC only tracks flood packets.
- **Outgoing SNR**: APC measures return-path SNR. It cannot know the SNR at the receiving end without protocol changes (e.g., trace route responses include this, but would require active probing).
- **Per-neighbor granularity**: APC produces a single global power reduction. It does not adjust power per destination — the radio can only set one TX power at a time.

## Files created/modified

| File | Action |
|------|--------|
| `include/mesh/PowerController.h` | **CREATE** — PowerController class |
| `src/PowerController.cpp` | **CREATE** — implementation |
| `include/mesh/Radio.h` | EDIT — added virtual `setTxPowerReduction`/`getTxPowerReduction` |
| `include/mesh/Mesh.h` | EDIT — added `_power_ctrl` member + accessors |
| `src/Mesh.cpp` | EDIT — 4 integration points |
| `adapters/radio/LoRaRadioBase.h` | EDIT — added `_tx_power_reduction_db` + override methods |
| `adapters/radio/LoRaRadioBase.cpp` | EDIT — apply reduction in `buildModemConfig()`, init member |
| `Kconfig` | EDIT — added `ZEPHCORE_APC` option |
| `CMakeLists.txt` | EDIT — conditional compile of `PowerController.cpp` |
| `helpers/CommonCLI.h` | EDIT — added APC callbacks (`getAPCReduction`, `getAPCMargin`, `isAPCEnabled`, `setAPCEnabled`, `getAPCTargetMargin`, `setAPCTargetMargin`) |
| `helpers/CommonCLI.cpp` | EDIT — added `get txpower`, `get/set apc.margin`, modified `set tx` for APC enable/disable |
| `app/RepeaterMesh.h` | EDIT — added APC callback overrides |
| `app/RepeaterMesh.cpp` | EDIT — added `_power_ctrl.setSF()` in `begin()` |
| `app/CompanionMesh.cpp` | EDIT — added `_power_ctrl.setSF()` in `begin()` and BLE param change |

## Verification

1. **Build test**: `west build -b rak4631 zephcore --pristine` and `west build -b wio_tracker_l1 zephcore --pristine` — both pass
2. **Kconfig disable**: Build with `-DCONFIG_ZEPHCORE_APC=n` — zero overhead
3. **CLI**: Flash a repeater, run `get tx` — should show `> 22dBm (apc=off)` initially (runtime default is off); after `set tx apc`, `> 22dBm (apc=on max=22 reduction=0 margin=0.0 target=16)`
4. **Functional**: Two nodes in close proximity (high SNR). After a few message exchanges, APC should reduce power. Check with `get txpower`.
5. **Staleness**: Power off the neighbor. Within ~2 minutes, `get txpower` should show power ramping back to max.
6. **Override**: `set tx 16` disables APC and fixes power. `set tx apc` re-enables.
7. **Margin config**: `set apc.margin 20` changes the target. `get apc.margin` confirms.
8. **Logging**: Enable `CONFIG_ZEPHCORE_LORA_LOG_LEVEL_DBG` to see APC state changes in log output.

## Resolved decisions

1. **No-echo = poor**: Packets sent but never echoed back count as margin=0 (conservative). Prevents over-reduction in sparse networks.
2. **Staleness**: 2-minute timeout. Ramps back at STEP_DOWN_DB per tick. Full recovery from max reduction in ~20s once triggered. Staleness and margin-based adjustment are mutually exclusive — when stale, APC never increases reduction (old EMA data is unreliable).
3. **Max reduction**: 12 dB. With SF8/BW62.5 and the no-echo-as-poor policy, APC is naturally self-limiting.
4. **Primary target**: SF8/BW62.5 (SNR threshold -10.0 dB, effective sensitivity ~-130 dBm). Algorithm works for all SF/BW combos via the threshold lookup table.
5. **Both roles**: APC is active for both companions and repeaters.
6. **Rogue filtering via SNR clustering**: When 2+ distinct echo sources are seen per packet, cluster their SNRs within 6 dB of the best. If only 1 source is in the top cluster (potential rogue), drop it and use the next source. With 1 source only, use its SNR directly.
7. **Per-entry SF tracking**: Each EchoEntry records the SF at track time (`sf_at_track`). Margin calculation uses `sfThresholdX4(sf_at_track)` so SF changes mid-flight don't corrupt margins.
8. **Echo window**: 10s (increased from initial 5s design to cover SF12 2-hop echoes at ~7s).
9. **CLI override**: `set tx <number>` disables APC and sets fixed power. `set tx apc` re-enables. Internal tracking continues when disabled for seamless resume.
10. **Configurable target margin**: `set apc.margin <6-30>` adjusts how conservative APC is. Higher values are safer for networks with hardware asymmetry (nodes with poor RX sensitivity). Default 16 dB. Resets on reboot.
11. **Companion APC**: No app changes needed — the app's TX power setting changes the ceiling, APC subtracts from it. The companion applies `prefs.apc_enabled` / `prefs.apc_margin` at `begin()` (both default like the repeater: off / 16 dB, persisted in prefs).
