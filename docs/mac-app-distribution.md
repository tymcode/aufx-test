# AU Effects Explorer — Mac app distribution

## Build and package

From the repo root:

```bash
./scripts/package_mac_app.sh
```

This produces:

- `dist/AU Effects Explorer.app`
- `dist/AU-Effects-Explorer-macOS.zip`

Optional Developer ID signing:

```bash
CODESIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)" ./scripts/package_mac_app.sh
```

Default signing is ad-hoc (`-`). **Notarization is not required for v1**; recipients use Gatekeeper “Open Anyway”.

Shipped builds target **macOS 13+** and are **universal** (`arm64` + `x86_64`).

The packaging script:

- Builds into `native/build-universal/` (keeps your day-to-day `native/build/` host-arch tree intact)
- Increments the build number in `native/plugin_host_app/VERSION` (semver is edited by hand)
- Asserts `lipo` reports both architectures before zipping
- Zips **without** AppleDouble `._*` resource-fork sidecars

Older zips that included those files fail codesign verification after unzip on another Mac (often reported as the app can’t be opened, sometimes with **-47**). If Gatekeeper still blocks a fresh unzip, clear quarantine:

```bash
xattr -cr "/path/to/AU Effects Explorer.app"
```
## First launch (recipient)

1. Unzip the archive to Applications (or anywhere).
2. If Gatekeeper blocks it: `xattr -cr "/path/to/AU Effects Explorer.app"`, then right-click → **Open**, or allow under **System Settings → Privacy & Security → Open Anyway**.
3. On first launch the app:
   - Seeds `~/Library/Application Support/AU Effects Explorer/host.config.json`
   - Scans installed Audio Units in an **out-of-process** worker (so a crashing/hanging AU cannot take down the host) and caches them as `plugin-cache.xml`
   - Lists failed plugins in the scan dialog and records them in `plugin-scan-skip.txt` so later rescans skip them
4. Use **Plugins → Add Plugin…** to add AUs to the list. **Plugins → Rescan Audio Units…** refreshes the cache.

Exploration captures land under the exploration data folder (default Application Support), in `sessions/`.

## Settings

**AU Effects Explorer → Settings…**

| Setting | Purpose |
|--------|---------|
| Exploration data folder | Sessions, logs, plugin cache, writable `host.config.json` |
| Config file override | Optional absolute path to a different `host.config.json` |
| Allow input to virtual instruments | Enable sampler/instrument audio inputs and feed the source clip into them |
| Skipped AU plugins | Select a plugin that crashed during scan, then **Retry selected** to rescan only that one |

Changes to path settings apply after relaunch. Instrument-input and skipped-plugin retries apply immediately.

## Path resolution

**Exploration data root**

1. `--project-root` / `--data-root`
2. User Settings (`explorationDataRoot`)
3. System plist `ExplorationDataRoot`
4. `~/Library/Application Support/AU Effects Explorer/` (bundle) or cwd (dev CLI)

**Config file**

1. `--config`
2. User Settings (`configPath`)
3. System plist `ConfigPath`
4. `<data-root>/host.config.json` (seeded from the bundle on first launch)

Fixtures ship in `Contents/Resources/fixtures/` and are used when the data folder has no local `fixtures/` directory.

## System defaults (managed Macs)

Write `/Library/Preferences/com.aufxtest.AUEffectsExplorer.plist` keys:

- `ExplorationDataRoot` — string path
- `ConfigPath` — string path to `host.config.json`

Example:

```bash
sudo defaults write /Library/Preferences/com.aufxtest.AUEffectsExplorer ExplorationDataRoot "/Users/Shared/AUFX"
```

## Plugins

Third-party Audio Units are **not** bundled. Install `.component` bundles to:

- `/Library/Audio/Plug-Ins/Components/`
- `~/Library/Audio/Plug-Ins/Components/`

## Dev launch (repo checkout)

Python is optional for launching the UI:

```bash
cmake -S native -B native/build -DCMAKE_BUILD_TYPE=Release
cmake --build native/build --target plugin_host_app

APP="native/build/plugin_host_app/plugin_host_app_artefacts/Release/AU Effects Explorer.app"
open "$APP" --args --config "$(pwd)/host.config.json" --project-root "$(pwd)"
```

If you already have a working `.venv`, `aufx-test host` is a thin wrapper for the same flags. If `source .venv/bin/activate` hits permission errors, recreate the venv or skip Python and use `open` as above — see [manual-exploration.md](manual-exploration.md).

Passing `--config` / `--project-root` keeps the existing repo-rooted workflow (cwd config + fixtures).