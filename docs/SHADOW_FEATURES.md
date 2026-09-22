# ShadoW extensions over ZephCore 1.17.4

- Companion SOS, GPS-wait notifications and tracking; Fall detection on
  supported boards. Fall overrides buzzer mute; manual SOS respects it.
- Board-specific charge indication and battery calibration. XIAO charging
  uses an independent green LED; M3/T1000/T-ECHO retain their charge priorities.
- M3 AHT10 temperature/humidity with enclosure correction. T1000 uses the
  upstream analog driver without MCU-temperature substitution.
- Repeater battery alerts: configurable destination, voltage threshold and
  interval; default on, 3250 mV, 12 hours. No forced GPS wake or shutdown.
- Repeater Bridge includes the repeater and supports bridge on/off, ping,
  key generation, peer configuration, transport and forwarding priority.
  Runtime help is the command catalogue for the selected role.
- Custom 433/868 radio profiles, minute-resolution display timezone and uptime.

## Settings migration

The upstream settings prefix remains unchanged. A versioned SHPF tail stores
custom fields in the same atomic prefs write. The deployed custom companion
202-byte layout and repeater 303-byte layout are read without interpreting their
custom bytes as upstream extra-SF/orientation fields. Bridge and battery settings
retain their separate files. Migration does not erase identity or contacts.
Upstream role-change formatting remains intentional and unchanged.

RP2040 retains upstream cryptographic RNG requirements: the old non-cryptographic
fallback is not restored. RP2040 builds require a compatible entropy provider.
The relaxed BLE authentication configuration applies only to repeater-bridge.

T1000 gestures: a short press arms SOS for three seconds, followed by a hold of
at least one second; six taps toggle tracking. Repeater GPS duty defaults to
12 hours. Upstream radio drivers, SDK and their patches are retained.
