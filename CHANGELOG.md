# Changelog

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
