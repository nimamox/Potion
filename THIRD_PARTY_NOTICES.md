# Third-party notices

Potion is Copyright (C) 2026 Potion contributors and is licensed as a whole
under GNU AGPL-3.0-or-later. The full project license is in `LICENSE`.

The following components are linked into or distributed with the Kindle
release. Their original licenses and notices continue to apply to them.

## libcurl 8.16.0

- Upstream: <https://curl.se/>
- Source: <https://github.com/curl/curl/tree/curl-8_16_0>
- License: curl license (MIT/X derivative), reproduced in
  `LICENSES/curl.txt`
- Use: statically linked into `potiond`

Copyright (C) Daniel Stenberg and the curl contributors.

## OpenSSL 3.0.18

- Upstream: <https://www.openssl.org/>
- Source: <https://github.com/openssl/openssl/tree/openssl-3.0.18>
- License: Apache-2.0, reproduced with the OpenSSL notice in
  `LICENSES/OpenSSL-Apache-2.0.txt`
- Use: statically linked into `potiond` through libcurl

Copyright OpenSSL contributors.

## katex-rs and KaTeX assets

- Native renderer: `katex-rs` 0.2.4, <https://github.com/MinusGix/katex-rs>
- Renderer license: MIT

- Upstream: <https://github.com/KaTeX/KaTeX>
- License: MIT, reproduced in `LICENSES/KaTeX-MIT.txt` and in the bundled
  `share/potion/vendor/katex/LICENSE`
- Use: `katex-rs` is statically linked into `potiond`; matching KaTeX 0.16.25
  CSS and fonts are bundled for local HTML layout. No KaTeX JavaScript ships.

Copyright (C) 2013-2020 Khan Academy and other contributors.

## Potion Fast Sans (Bionic Reading)

- Fast-Font upstream: <https://github.com/Born2Root/Fast-Font>, commit
  `aeae0775d9251365eae3b133cbf26ce0366f6108`
- Fast-Font license: MIT, reproduced in `LICENSES/Fast-Font-MIT.txt`
- Base typeface: Inter, <https://github.com/rsms/inter>
- Inter license: SIL Open Font License 1.1, reproduced in
  `LICENSES/Inter-OFL-1.1.txt`
- Use: four locally bundled, renamed Potion Fast Sans faces provide the
  optional experimental Bionic Reading presentation for reader prose.

Copyright (c) 2023 Born2Root. Inter copyright (c) 2016 The Inter Project
Authors. The generated derivative font family remains under the SIL Open Font
License 1.1; Potion's application code remains AGPL-3.0-or-later.

## Noto Emoji (monochrome)

- Upstream: <https://github.com/googlefonts/noto-emoji>, commit
  `2f1ffdd6fbbd05d6f382138a3d3adcd89c5ce800`
- Bundled file: static monochrome `NotoEmoji-Regular.ttf` version 1.05;
  SHA-256 `415dc6290378574135b64c808dc640c1df7531973290c4970c51fdeb849cb0c5`
- License: SIL Open Font License 1.1, reproduced in
  `LICENSES/NotoEmoji-OFL-1.1.txt`
- Use: process-local Fontconfig and reader font stacks use Noto Emoji only as
  a fallback for glyphs absent from the selected Kindle or Potion font. Potion
  does not install a system font or ship Noto Color Emoji.

Copyright 2013 Google Inc. All Rights Reserved.

## GNU runtime libraries

The Kindle bundle includes unmodified runtime files from Debian cross packages:

- glibc 2.41 (`libc6-armel-cross` and `libc6-armhf-cross` 2.41-11cross1):
  the matching ARMEL/ARMHF loaders, `libc.so.6`, and `libm.so.6`;
  LGPL-2.1-or-later. Source:
  <https://sources.debian.org/src/glibc/2.41-11/>
- GCC 14.2.0 (the ARMEL and ARMHF cross-runtime packages, 14.2.0-19cross1):
  `libgcc_s.so.1` and `libstdc++.so.6`; GPL-3.0-or-later with GCC Runtime
  Library Exception 3.1. Source:
  <https://sources.debian.org/src/gcc-14/>

The applicable texts are reproduced in `LICENSES/LGPL-2.1-or-later.txt`,
`LICENSES/GPL-3.0-or-later.txt`, and `LICENSES/GCC-exception-3.1.txt`.

## Certificate authorities

`etc/ca-certificates.crt` comes from Debian `ca-certificates` 20250419 and
contains Mozilla CA certificate data. Mozilla-derived material is available
under MPL-2.0, reproduced in `LICENSES/MPL-2.0.txt`. Source:
<https://sources.debian.org/src/ca-certificates/20250419/>.

## Trademarks and services

Potion is an independent project and is not affiliated with or endorsed by
Notion Labs, Inc. “Notion” is used only to identify compatibility with the
Notion service. No Notion software library is included in Potion.

The original Potion names, logos, thumbnails, and other project artwork in
this repository are licensed under AGPL-3.0-or-later with the rest of Potion.
No trademark license is granted by the software license.
