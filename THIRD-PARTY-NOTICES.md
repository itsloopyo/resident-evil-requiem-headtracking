# Third-Party Notices

This project uses the following third-party software:

## REFramework

- **Author:** praydog
- **License:** MIT
- **URL:** https://github.com/praydog/REFramework
- **Nightly builds:** https://github.com/praydog/REFramework-nightly
- **Usage:** Plugin host and SDK for RE Engine games. Provides method hooking, type system access, and per-GUI-element draw callbacks. Bundled in release ZIP as fallback at `vendor/reframework/RE9.zip` (pinned to a specific nightly); fetched latest nightly at install time by `vendor/reframework/fetch-latest.ps1`. `install.cmd` only installs it if the user does not already have a REFramework install. Not modified.

See `vendor/reframework/LICENSE` (MIT) and `vendor/reframework/README.md` (pinned nightly tag + commit + SHA-256) for the bundled snapshot's full provenance.

## OpenTrack

- **License:** ISC
- **URL:** https://github.com/opentrack/opentrack
- **Usage:** Head tracking data is received via the OpenTrack UDP protocol. No OpenTrack code is bundled.

## CameraUnlock Core Library

- **Author:** itsloopyo
- **License:** MIT
- **URL:** https://github.com/itsloopyo/cameraunlock-core
- **Usage:** Shared C++ library providing UDP receiver, tracking processing pipeline, smoothing, interpolation, hotkey input, and math utilities. Compiled into the plugin DLL.
