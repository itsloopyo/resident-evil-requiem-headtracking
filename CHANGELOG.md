# Changelog

## [0.4.0] - 2026-08-25

### Added

- flashlight beam follows the head, leading the view at 1.5x

### Fixed

- GUI compensation holds under head roll and stops dragging the HUD
- project the reticle through the engine's own matrices, not by hand
- bake the position axis conversion in, drop the Invert knobs
- the reticle takes lean parallax, scaled to what the bullet stops on

## [0.3.0] - 2026-08-20

### Added

- tracker owns centring, smoothing splits into LocalSmoothing and RemoteSmoothing

### Fixed

- use flat 2D roll rotation for marker compensation to stop off-center drift
- show full control set in pixi install via shared -Controls

## [Unreleased]

### Logging

- Removed `HeadTracking_diag.csv`. It was written every frame with a flush per row, about 20 MB an hour of disk traffic on the render thread, always on and documented nowhere. `F9` hides and shows the world-anchored GUI markers; it never placed a marker in the log, and the unreachable code that claimed to has been removed.
- Capped the four per-frame crosshair/marker projection traces at five lines each per session. They ran every 120 frames for the whole session, about 1.3 MB an hour at 60 fps into REFramework's log, which buried the startup lines a user is asked to send.
- The log now names the config file it actually read (`Config Canonical: <path>`, or `Created` or `Migrated` on a first start), so an edit made to the wrong file is visible in the log instead of costing a support round trip.
- A one-shot `First tracker pose received: yaw/pitch/roll (local|remote connection)` line the first time a tracker packet reaches the mod. It is emitted ahead of every enable/gameplay gate, so its absence means the packets never arrived rather than that tracking was off or the camera hook had not engaged.
- Troubleshooting now names the log file to send (`<game>/re2_framework_log.txt`, truncated per launch) and the startup lines to look for in it.

### Changed
- Settings move to `reframework\plugins\CameraUnlock.ini`. Earlier versions of the mod kept these settings in `HeadTracking.ini`, in the same folder. The first time this version starts and finds no `CameraUnlock.ini`, it reads your settings from `HeadTracking.ini` and writes them into `CameraUnlock.ini`. It never changes `HeadTracking.ini`, and does not read it again while `CameraUnlock.ini` exists.
- A setting that the defaults the README shows set to `default` is written as `default` when you never changed it from the default earlier versions used, because `HeadTracking.ini` does not hold it or holds that default. It then follows `Defaults.ini`, so it takes the value `Defaults.ini` gives it, or the built-in value where `Defaults.ini` gives none, which can differ from the default earlier versions used. A setting you changed is written with the value imported for it, or as `default` where that value equals its default at that start.
- Comments, and keys the mod never read, are not carried over. Nor are these, where your old file had them:
  - A sensitivity, scale, deadzone, response curve or axis inversion you changed from its default. Set these in your tracker instead.
  - Reticle settings, and a key that toggled the reticle.
  - The setting for a feature that earlier versions shipped switched off while it was untested. It now follows the mod's default.
- An older version of the mod reads `HeadTracking.ini` and never reads `CameraUnlock.ini`, so a setting you change after updating is not in `HeadTracking.ini`.
- Deleting only `CameraUnlock.ini` makes the next start read `HeadTracking.ini` again. To go back to the defaults, replace everything in `CameraUnlock.ini` with the defaults the README shows. Every setting they set to `default` then follows `Defaults.ini`.
- Hotkeys are written as key names, and each hotkey lists every key that triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`.
- Turning positional tracking off or on with `Page Up` / `Ctrl+Shift+G`, and switching the yaw mode with `Page Down` / `Ctrl+Shift+H`, is saved to `CameraUnlock.ini` straight away, so the next start keeps your choice. Turning head tracking on or off with `End` still lasts for the session only.
- The keys are renamed to the names every head tracking mod on this format uses: `[Network] UDPPort` is `UdpPort`, `[General] AutoEnable` is `EnableOnStartup`, `[Position] Enabled` is `PositionEnabled`, `[Position] LimitX`, `LimitY`, `LimitZ` and `LimitZBack` are `PositionLimitX` and so on, `[Hotkeys] PositionToggleKey` is `CycleTrackingModeKey`, and `[Flashlight] Enabled` and `Multiplier` are `[Light] LightFollowsHead` and `LightMultiplier`. The import carries each value across.
- Since 0.4.0, and not caused by the move to `CameraUnlock.ini`, reading the settings changed in 83ef3da, which moved the mod onto the shared plugin code:
  - A number followed by anything but an inline comment (`0,15` or `0.5abc`), or written in hex (`0x1`), keeps the setting's default. 0.4.0 read the number at the front of the text, so `LocalSmoothing=0,15` gave 0.
  - A position limit below 0.01 is kept as written, down to 0. 0.4.0 raised it to 0.01.
  - `LimitY` sets how far the view moves down as well as up. 0.4.0 held downward travel at 0.20 m whatever `LimitY` said.
  - A hotkey code the mod no longer accepts as a hotkey (0, a negative code, one above `0xFE`, or Shift, Ctrl or Alt, which the chords are made of) keeps that hotkey's default key. 0.4.0 registered the code as written.
  - A sensitivity outside 0.4.0's range is read as written up to 5 (rotation) or 10 (position), where 0.4.0 clamped it. Either way it is not carried over, as above.
- `Page Up` / `Ctrl+Shift+G` turns positional tracking off and on again instead
  of cycling three modes. The third mode disabled head rotation, and it sat
  directly after the mode a `[Position] Enabled=false` config starts in, so one
  press of a key labelled "toggle position" switched head rotation off.
- Recentring is gone entirely: the `Home` / `Ctrl+Shift+T` hotkey, the
  `RecenterKey` ini entry, and the mod's own centre. Your tracker owns the
  centre now. Set it there, with OpenTrack's Center bind, the CENTER button in
  a phone app, or your headset's own centring, and the mod applies what the
  tracker sends.
  Two centres in series was the problem: when the view was off you could not
  tell which side was wrong, and switching trackers meant centring in both.
- Smoothing is now two user-configurable parameters in a new `[Smoothing]` section of `HeadTracking.ini`: `LocalSmoothing` (default 0.0) for a tracker running on this machine, and `RemoteSmoothing` (default 0.15) for a tracker on a remote network device. The value is picked per connection from the packet source address and is re-evaluated while the game runs, so switching between a local OpenTrack instance and a phone on WiFi takes effect without a restart.
- Removed the `[Position] Smoothing` key. Both new parameters cover rotation and position, so there is no separate position smoothing setting.
- Removed the hidden 0.15 baseline smoothing floor that silently overrode the configured value. Local users now get zero-latency tracking by default.

### Added

- A setting set to `default` in `CameraUnlock.ini` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it, and neither do earlier versions of this mod. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.
- `Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.
- When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that.

### Removed

- The sensitivity, scale, deadzone, response curve and axis inversion settings (`[Sensitivity] YawMultiplier`, `PitchMultiplier` and `RollMultiplier`, and `[Position] SensitivityX`, `SensitivityY` and `SensitivityZ`). Set these in your tracker app instead.
- With these settings at 1.0, which the installer and the Nexus ZIP of 0.3.0 and 0.4.0 shipped, the camera moves as it did before. A `HeadTracking.ini` that Lopari seeded from 0.3.0 or 0.4.0 holds a position sensitivity of 2.0, as did the file 0.1.0 to 0.2.2 shipped; that value is not carried over, so for those players leaning moves the view half as far as it did, up to the same position limits.
- The installer and the Nexus ZIP no longer carry a config file, and the launcher manifest no longer seeds one: the mod creates `CameraUnlock.ini` when it first starts.

## [0.2.2] - 2026-06-07

### Added

- add HeadTrackingSession and expand C++ core with RE Engine, Unreal, and tracking-session modules
- aim projection, reframework/unreal hooks, input/logging hardening, games
- add Mass Effect Legendary Edition to games catalog
- expand games catalog, fix unicode games.json read, stage launcher manifest
- add Pacific Drive to games catalog
- add Homeworld: Remastered Collection to games catalog
- add manifest-mode installer validator and ASI loader subdir support
- authenticate GitHub API requests via env token when present
- add R.E.P.O. detection data

### Fixed

- fail fast in ASI dev-deploy when the game is running
- restore il2cpp camera position by undoing applied local delta
- set SO_REUSEADDR so the receiver reclaims its port on relaunch

### Other

- Add Ubisoft Connect detection and VendorZip BepInEx install
- Add PluginSubfolder param to Invoke-DevDeployBepInEx
- Add Xbox install path for Easy Delivery Co
- Add GOG IDs for Cyberpunk 2077
- Add PLUGIN_SUBFOLDER support to BepInEx install/uninstall bodies
- scripts: drop the two-phase loader-init prompt from install bodies
- data: add Black & White (Lionhead) to games registry
- scripts: detect BepInEx 6 IL2CPP via BepInEx.Core.dll marker
- powershell: skip cameraunlock-core remote refresh in CI
- scripts: add UE4SS install template, fix delayed expansion in ASI body, expand games registry
- protocol: reject finite-but-out-of-float-range packet values
- data: add Subnautica 2 to games registry
- detection: add installer-registry game path lookup (Black & White GameDir)
- protocol: reorder tracking data member in udp_receiver
- data: fix Subnautica 2 Steam app id (3367150 -> 1962700)
- data: add Ni no Kuni Remastered and Yakuza 0; switch find-game output to UTF-8
- detection: add Xbox/GDK build support for Subnautica 2 (and any future GDK title)
- find-game: escape `&` in GAME_DISPLAY_NAME so echo doesn't split
- templates: add uninstall.ps1; data: add Deus Ex Mankind Divided
- powershell: add NightlyRelease module for Patreon-gated nightly builds
- protocol: disable SIO_UDP_CONNRESET and add one-shot receiver diagnostics; powershell: write nightly manifest.json without UTF-8 BOM; data: add Mixtape
- powershell: stop redirecting git stderr in Update-CameraUnlockCoreToRemoteTip
- powershell: publish dev builds as GitHub pre-releases
- protocol: disable SIO_UDP_CONNRESET and add one-shot receiver diagnostics
- data: add Mixtape
- powershell: stop redirecting git stderr in Update-CameraUnlockCoreToRemoteTip
- powershell: run gh under Continue so its stderr doesn't abort the dev-release publish
- core: bump submodule to strip VR runtime DLLs for flatscreen install
- reframework: strip VR runtime DLLs on install for flatscreen mode
- reframework: cache GetValue method and avoid per-call heap in ArrayGetValue; data: add BioShock Infinite
- uninstall: remove reframework_revision.txt marker dropped at game root
- install: render MOD_CONTROLS multi-line via percent expansion
- Add YAPYAP to games.json
- powershell: write state file BOM-less so Lopari JSON parser accepts it
- core: adopt cameraunlock-core shared helpers, bump REFramework
- powershell: stop redirecting git stderr in Invoke-VersionCommit

## [0.2.1] - 2026-05-03

### Other

- Add DX11 overlay header for crosshair rendering
- Update PositionInterpolator tests for bounded extrapolation
- Skip vendor refresh when SHA-256 matches existing copy
- Fix degenerate-input bugs in scanners, projection, and color parser
- Add yaw-mode key and WorldSpaceYaw config options
- Quote /y flag detection and add shared install/uninstall bodies
- Add DevDeploy module with Cecil dev-install orchestrator
- Auto-refresh cameraunlock-core submodule in Copy-SharedBundle
- Add install bodies and dev-deploy orchestrators for non-Cecil frameworks
- Resolve exe relpath from games.json in ASI/shim dev-deploy
- Add automatic port retry to C++ UdpReceiver
- Take BuildOutputPath in dev-deploy and add loader/config auto-install
- Verify existing BepInEx loader arch and replace on mismatch
- Fall back to dev-tree vendor path in BepInEx install body

## [0.2.0] - 2026-05-01

### Added

- add Invoke-FetchLatestLoader and Refresh-VendoredLoader helpers

### Other

- Vendor REFramework and rework HUD marker projection
- Gate UnityEngine.InputLegacyModule reference on file existence
- Fix batch paren-poisoning in install.cmd template
- Move game detection to data-driven games.json
- Fix install.cmd/uninstall.cmd templates for dev-tree use
- Unify installer CLI across BepInEx/MelonLoader/Cecil/ASI/REFramework/shim
- Fix HUD marker drift via direction-space rotation compensation
- Make vendored loaders the install-time source of truth
- Support major/minor/patch bump arguments in release script
- Add Step-SemanticVersion and Resolve-ReleaseVersion helpers
- Add chord hotkeys and 3-state tracking mode cycle
- Add camera discovery module (RTTI vtable + float classifier)
- Add AGENTS.md with shared code-quality and library API rules
- Sync installer scripts with cameraunlock-core /y flag fix
- Expand submodule pointer commits in generated changelogs
- Fix /y flag detection and bundle vendored BepInEx in installers
- Tick smoothing pipeline once per render frame
- Use WriteAllBytes for .cmd output to avoid Defender race

## [0.1.2] - 2026-04-15

### Other

- Lower default position sensitivity from 2.0 to 1.0

## [0.1.1] - 2026-04-15

### Changed

- Update cameraunlock-core: interpolator velocity extrapolation

## [0.1.0] - 2026-04-14

First release.
