# reframework (vendored)

This directory contains a bundled copy of the upstream mod loader. It is the install-time
source of truth: install.cmd extracts directly from here and never reaches out to the network.
Refresh manually with `pixi run update-deps`, then commit.

## Snapshot

- Asset: `REFramework.zip`
- Tag: `nightly-01373-c4b1314820d20255febf7834903e8cedb669b49c`
- Commit: `0436e043af6f81a5d3fef49ae27d35e63431e566`
- Upstream URL: https://github.com/praydog/REFramework-nightly/releases/download/nightly-01373-c4b1314820d20255febf7834903e8cedb669b49c/REFramework.zip
- SHA-256: `cb1cbcfcb7e7a93f4b4c775b4c426d060b117a4a25526c447c67f048555803cf`
- Fetched at: 2026-05-03T19:23:00.3039690+01:00
- Source: github

Do not edit this directory by hand. Run ``pixi run package`` (or CI release) to refresh.
