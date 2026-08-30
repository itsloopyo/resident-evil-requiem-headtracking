# Third-Party Notices

RE9HeadTracking bundles, statically links, or credits the third-party components
listed below. Each remains the property of its authors and is used under its own
licence. Where a licence requires the copyright notice, the conditions and the
disclaimer to accompany a binary distribution, the full text is reproduced here
verbatim, and this file ships at the root of every release ZIP we publish.

Nothing in this repository is derived from, or redistributes any part of,
Resident Evil Requiem.

| Component | Version | Licence | How it ships |
|-----------|---------|---------|--------------|
| REFramework | nightly-01394-ec6c81fd39831b328027ae00e102bc9c9c3f8aa5 | MIT | Bundled verbatim in the installer ZIP |
| REFramework plugin SDK headers | plugin API 1.15.0 | MIT | Redistributed verbatim as source in this repository |
| cameraunlock-core | b5b4d9888630ca9591a1f7736c2c1785940609a0 | MIT | Compiled into `RE9HeadTracking.dll` |
| OpenTrack | n/a | ISC | Not bundled; UDP protocol interoperability only |

---

## REFramework

praydog's work reaches this repository in two separate copies, both verbatim and
both under the MIT licence reproduced below.

**The loader binary**, vendored at `vendor/reframework/`, shipped in the
installer ZIP and used as the install-time source. Taken from the upstream
release asset untouched; the upstream licence file ships beside it at
`vendor/reframework/LICENSE`.

- Upstream: https://github.com/praydog/REFramework
- Version: `nightly-01394-ec6c81fd39831b328027ae00e102bc9c9c3f8aa5`
- Commit: `0436e043af6f81a5d3fef49ae27d35e63431e566`
- SHA-256: `a3d24f04e41933a7a3a6e1d6402b7de18ca677245d9ca0dda9f6a5ca20e9b94e`

**The plugin SDK headers**, `extern/reframework/API.h` and
`extern/reframework/API.hpp`, redistributed in source form so the plugin can be
built from a clean checkout. Both are byte-identical to the upstream files under
`include/reframework/`, unmodified, and declare plugin API version 1.15.0. The
upstream licence file ships beside them at `extern/reframework/LICENSE`. They
are headers only: no REFramework implementation code is copied, and the loader
itself is resolved at runtime.

```
MIT License

Copyright (c) 2019 praydog

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## cameraunlock-core

Git submodule at `cameraunlock-core/`, compiled into `RE9HeadTracking.dll`. Our own code,
MIT licensed, reproduced here so the notices are complete.

- Pinned commit: `b5b4d9888630ca9591a1f7736c2c1785940609a0`

```
MIT License

Copyright (c) 2026 CameraUnlock

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## OpenTrack

Not bundled and not linked. This mod implements the OpenTrack UDP pose datagram
layout so that OpenTrack (https://github.com/opentrack/opentrack, ISC licence)
and compatible trackers can drive it. No OpenTrack code, headers or binaries
are copied, linked or redistributed, so its licence triggers no notice
obligation here. It is credited because the wire format is its work.

---

## Resident Evil Requiem footage and screenshots

- **Files:** `assets/readme-clip.gif`
- **Rights holder:** the developers and publishers of Resident Evil Requiem, together with the
  rights holders of any third-party marks visible in frame.
- **Usage:** recorded from the game running with this mod, captured on a
  legitimately purchased copy, shown so a reader can see what the mod does
  before installing it.
- **Bundled:** `assets/readme-clip.gif`: kept in this repository only. The packaging scripts
  ship no part of `assets/`, so these are in neither release ZIP nor
  anything the launcher deploys.
- **Licence:** none is granted or implied by this repository. This material is
  not covered by the MIT licence in `LICENSE`, and nothing here permits reuse
  of it. Rights holders who would rather it were not published: open an issue
  or reach us on Discord and it comes down.

---

## Resident Evil Requiem

Resident Evil Requiem and all related names, logos, characters and marks are
trademarks of their respective owners. They are used here only to identify the
game this mod applies to, which is nominative use and not a claim of any right
in them. This project is an unofficial, fan-made modification. It is not
affiliated with, endorsed by, or sponsored by the game's developers, its
publishers, its engine vendor, or any other rights holder. It redistributes no
game code, no game assets and no proprietary DLLs, and it requires a
legitimately purchased copy of the game. Any engine structure offsets,
function addresses or byte patterns referenced in the source were derived by
the authors through independent analysis of a legitimately owned copy. They
are factual measurements recorded as numbers; no decompiled or disassembled
game code is stored in this repository.
