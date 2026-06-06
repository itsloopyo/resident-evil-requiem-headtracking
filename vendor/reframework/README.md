# reframework (vendored)

This directory contains a bundled copy of the upstream mod loader. It is the install-time
source of truth: install.cmd extracts directly from here and never reaches out to the network.
Refresh manually with `pixi run update-deps`, then commit.

## Snapshot

- Asset: `REFramework.zip`
- Tag: `nightly-01380-2130fe73045b9508fb491283f773d497082c5d8c`
- Commit: `0436e043af6f81a5d3fef49ae27d35e63431e566`
- Upstream URL: https://github.com/praydog/REFramework-nightly/releases/download/nightly-01380-2130fe73045b9508fb491283f773d497082c5d8c/REFramework.zip
- SHA-256: `393d4a806adc8b8bde78f0d4fae9bbb2c5122526604ba882bf941088ca2b3e45`
- Fetched at: 2026-05-31T23:47:35.1034308+01:00
- Source: github

Do not edit this directory by hand. Run ``pixi run package`` (or CI release) to refresh.
