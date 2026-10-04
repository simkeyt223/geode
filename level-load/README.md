# Level Load

Speeds up level loading. Updated for **Geode v5.10.x / Geometry Dash 2.2081**.

## Building

The Geode build checks that `mod.json` matches the SDK, so keep `"geode": "5.10.x"` and `"gd": "2.2081"` in sync with the SDK you build against.

**Easiest (no local setup):** push this folder to your own GitHub repo. The included workflow
(`.github/workflows/multi-platform.yml`) builds Windows, macOS, iOS, Android32 and Android64 and
produces one combined `.geode` under *Actions → latest run → Artifacts → Build Output*.

**Locally (Windows):** install the [Geode CLI](https://docs.geode-sdk.org/getting-started/geode-cli), run `geode sdk install` / `geode sdk update`, then `geode build` in this folder.

## Notes for future game updates
This mod re-implements parts of the game's own level-loading code (object creation, group setup).
After a GD update, re-check those against the new version. The `fast-mode` setting falls back to the game's code.
