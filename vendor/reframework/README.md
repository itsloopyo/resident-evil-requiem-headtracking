# reframework (vendored)

This directory contains a bundled copy of the upstream REFramework nightly used as the install-time source.
Vendored is the single source of truth for installs; users get exactly this version. To bump it:

    pixi run update-deps

then commit the refreshed vendor/ tree.

## Snapshot

- Asset: `RE9.zip`
- Tag: `nightly-01366-6216ec39697c5b3469e08baf0b98db0baff49c49`
- Upstream URL: https://github.com/praydog/REFramework-nightly/releases/download/nightly-01366-6216ec39697c5b3469e08baf0b98db0baff49c49/RE9.zip
- SHA-256: `75d52ba1ced856075d4d86327342e9aeffb72c1aeac922bc4312cb56709f9a92`
- Fetched at: 2026-04-25T15:43:26.9844022+01:00
- Source: github (praydog/REFramework-nightly)

Do not edit this directory by hand. Run `pixi run update-deps` to refresh.
