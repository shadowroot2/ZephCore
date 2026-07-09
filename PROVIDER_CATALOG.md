# Mesh America configurator — provider catalog

ZephCore firmware is published to the [Mesh America Device Configurator](https://apps.meshamerica.com)
as a third-party provider catalog. Users pick a board in the app and flash ZephCore
directly from the browser.

## What to send Mesh America

```
Provider name: ZephCore
Catalog URL:   https://raw.githubusercontent.com/liquidraver/ZephCore/firmware-dist/catalog.json
```

(Send this once, after the first `master` build has run and created the `firmware-dist`
branch. Validate first at <https://apps.meshamerica.com/validate>.)

## How it works

`gen_provider_catalog.py` turns a release's firmware assets into `catalog.json`
(the provider-spec schema: `version -> {notes, files:[{type,name,url}]}` with
absolute HTTPS URLs). The `build firmware` workflow runs it in the
`publish-catalog` job and force-pushes the firmware + catalog to the
`firmware-dist` orphan branch on every `master` build.

**Hosting / CORS.** The configurator runs in the browser and `fetch()`es both the
catalog and each firmware binary, so every response needs
`Access-Control-Allow-Origin: *`. GitHub **Release assets do not** send it (they
redirect to `release-assets.githubusercontent.com`, which omits the header), so we
cannot serve firmware from the normal release. Instead:

| Artifact | Host | Why |
|----------|------|-----|
| `catalog.json` | `raw.githubusercontent.com/.../firmware-dist/catalog.json` | CORS `*`, always fresh (raw doesn't cache branch refs) |
| firmware binaries | `cdn.jsdelivr.net/gh/liquidraver/ZephCore@firmware-dist/<file>` | CORS `*`, CDN-backed; filenames carry the git hash so they're effectively immutable |

The normal timestamped GitHub Release (with all the same binaries) is still created
for manual downloads and OTA — the configurator pipeline is additive.

## Device mapping

Boards whose `name` matches an official MeshCore device **fold** into that device
(our firmware shows as extra options under the existing tile). Boards MeshCore
doesn't have become **new** ZephCore-badged tiles. The mapping table lives in
`BOARDS` in `gen_provider_catalog.py`; adjust it there when boards are added or
renamed. Names must match MeshCore's canonical `config.json`
(`meshcore-dev/flasher.meshcore.io`) **exactly** to fold — the validator flags
mismatches.

Notes on the mapping:
- **ESP32 offers `flash-wipe` only — because of an offset mismatch, not flashability.**
  Our `-update.bin` (the MCUboot-signed app) *is* serial-flashable, but only at ZephCore's
  app-slot offset `0x20000` (`slot0_partition: partition@20000` in every ESP32 overlay;
  MCUboot itself owns `0x0`–`0x20000`). Mesh America's `flash-update` hardcodes `0x10000`
  (MeshCore's slot layout) and the catalog schema has no offset override, so it would write
  the app inside our MCUboot partition and the board wouldn't boot. `flash-wipe` (full merged
  image at `0x0`) is offset-independent and always works. Enabling `flash-update` would mean
  moving our MCUboot partition to `0x10000` to match MeshCore — out of scope.
- **Companion firmware** is one image serving BLE + USB; it's listed under both
  `companionBle` and `companionUsb` (same file). Native-Linux companions use a
  custom `companionTcp` role.
- **Heltec T114** lists screen and "No screen" variants (subTitle) under one tile.
- **Native-Linux** boards (femtofox, rak6421) are `noflash` download-only tiles.
- **Thumbnails:** folded devices inherit MeshCore's image automatically (device-level
  fields come from the official entry on merge), so only the new tiles set a `tooltip`.
  Those reuse the configurator's own image set via jsDelivr (`IMG_BASE` in the script);
  boards without dedicated art fall back to a neutral LoRa icon. Swap in ZephCore-branded
  art by pointing `img` at your own absolute HTTPS URLs.

## Local test

```
python gen_provider_catalog.py --firmware-dir firmware \
  --url-base "https://cdn.jsdelivr.net/gh/liquidraver/ZephCore@firmware-dist" \
  --version v0.0.0 --out catalog.json
```

It validates the output against the spec's hard requirements and prints a
fold/new summary; a non-zero exit means a schema error.
