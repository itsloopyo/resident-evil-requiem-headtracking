# reframework (vendored)

This directory contains a bundled copy of the upstream mod loader. It is the install-time
source of truth: install.cmd extracts directly from here and never reaches out to the network.
Refresh manually with `pixi run update-deps`, then commit.

## Snapshot

- Asset: `RE9.zip`
- Tag: `nightly-01366-6216ec39697c5b3469e08baf0b98db0baff49c49`
- Commit: `0436e043af6f81a5d3fef49ae27d35e63431e566`
- Upstream URL: https://github.com/praydog/REFramework-nightly/releases/download/nightly-01366-6216ec39697c5b3469e08baf0b98db0baff49c49/RE9.zip
- SHA-256: `75d52ba1ced856075d4d86327342e9aeffb72c1aeac922bc4312cb56709f9a92`
- Fetched at: 2026-04-30T21:58:20.8741927+01:00
- Source: github

Do not edit this directory by hand. Run ``pixi run package`` (or CI release) to refresh.
