# Author-first integration — 2026-09-22

## Completion update — 2026-09-23

Source overlay completed under the user's instruction to finish the merge.
The progress notes below are historical, superseded by this section.
Settings use the author's prefix plus a versioned SHPF tail in the same
atomic write; deployed custom 202/303-byte layouts are migrated on read.
Bridge/battery preference files retain their existing format. Weak RP2040 RNG
fallback is not restored; the author CSPRNG requirement remains.
README/architecture link the custom feature and migration guide.

Syntax sweep: 48 T1000 units, 43 M6 bridge units and 46 Heltec bridge units.
Non-radio units passed after fixing repeater buzzer-mode integration.
Unchanged author radio adapters cannot be validated against the old generated
SDK headers (new APIs are present in the author's patches). Fresh configuration,
firmware builds and hardware tests have NOT run. No claim of runtime validation.

Base: upstream 1.17.4-zephcore (`eef6a48`). Custom source: release
`b92f8eb`, compared against actual merge base `1976018`.
The unfinished selective import is a backup only (`3fea071`), not a source.

Working branch: `codex/author-first-1.17.4`.
Original dev checkout is untouched except for a context pointer.

90 independent custom paths copied; 28 files merged without textual conflicts;
25 technical conflicts resolved, including CMake, main loops, mesh handlers,
board/UI adapters and UI action bits. This is not
a completed integration: semantic main-loop review, CLI, prefs serialization and UI overlaps
remain pending. Radio drivers, SDK and radio patches remain author-original.
No build, runtime test or hardware verification has been performed here.
Syntax-only checks using existing generated configs passed for buzzer,
ZephyrBoard, joystick hooks/task, CompanionMesh and main_companion.
Fixed duplicate SimpleLPP luminosity method and obsolete buzzer UI calls.
Keep author TTGO queue size 32 (do not restore custom 100) and common logging.
Use author env_data luminosity fields; preserve custom GPS satellite mapping.
Use author-first-overlay.json as the per-file checklist.

Latest pass: CommonCLI minute timezone command overlays author parser (no legacy
CommonCLI serializer restored); whole-hour tz.offset updates minute state too.
Observer retains author's single first-boot initialization. Button pages/task
combine custom SOS/tracking/bridge and T-ECHO backlight with author input flip,
wake-event handling and buzzer modes. Joystick system page uses minute timezone
only after UTC clock synchronization. Syntax checks passed for CommonCLI,
button pages/task (Heltec), task (T1000), and synthetic joystick system.
Charge/LED arbitration in ui_common and environmental sensors are now ported;
semantic/hardware verification remains pending.
Timezone persistence/import normalization depends on the pending prefs migration.

User confirmed author T1000 analog driver. Keep custom always-on sensor rail and
accelerometer power gate, charger input and gestures. P0.04 has one owner:
t1000_sensor_enable regulator; omit analog driver's optional power-gpios.
M3 keeps AHT10 initialization/retry and -5000 mC correction. No MCU-temperature
fallback on M3/T1000. Other author's sensor discovery and deferred-init retained.
Heartbeat modes gate normal/unread indication, not charge indication. Shared
radio LED ownership retained. XIAO charging remains independent.
ui_common and EnvSensors passed syntax-only on existing M3, T1000, XIAO,
Heltec and T-ECHO configs (new AHT offset default supplied explicitly on older
Heltec/T-ECHO configs). New T1000 DTS/analog driver path still needs validation.

Confirmed decisions:
- Keep the author's ESP32 stack protection. Do not copy our broad disable.
- Preserve our BLE security mode ONLY in repeater-bridge; companion stays author-based.
- Preserve custom 433/868 presets, Fall/SOS and charge/LED functionality.
- Earlier decisions remain: author guest behavior and role-change formatting.
- RP2040 no-CSPRNG fallback awaits a user decision. Author RNG is untouched.
- Prefs layout collision awaits a decision: author format + versioned custom
  extension and legacy migration recommended. Do not blindly overlay serializers.

The author moved zephcore/ARCHITECTURE.md to docs/ARCHITECTURE.md. Port the
small custom documentation delta there; do not resurrect the old path.
Do not treat unchanged individual files as proof of semantic compatibility:
BLE configuration extraction and new board files still need integration review.
