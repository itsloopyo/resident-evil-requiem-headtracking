# Changelog

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
