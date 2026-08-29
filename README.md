![Potion](logo/potion_logo_orig_size.png)

# Potion

Potion is a lightweight Notion reader for jailbroken Kindles. A C++17 daemon
uses Notion's official API, converts Notion-flavored Markdown to sanitized
HTML, and serves a deliberately lightweight interface to Amazon Mesquite.

Potion can validate a connection token, search accessible pages, retrieve page
metadata/Markdown, and fetch images. Within ordinary paragraphs and list items,
selected text can be highlighted, bolded, or underlined directly on Notion.
This formatting requires the integration's update-content capability; Potion
does not cache page content or keep a local copy of edits.
On Kindle, hold a word to select it; dragging while holding extends Potion's
selection when Mesquite supplies drag events. Potion also accepts an ordinary
WebKit selection when the device provides native selection behavior.
The reader supports navigable child pages, parent-page Back navigation, regular
toggles, toggle headings, and Notion text/background highlights. Night mode can
preserve page colors, adapt them for a dark background, or also invert images.
The reader toolbar keeps navigation unambiguous: Back follows page history,
while Pages always returns directly to the main page list.
The settings dialog also supports normal or reversed Kindle physical page-button
scrolling and provides contextual help for these display/input choices.
Authentication and settings are stored by `potiond`, not Mesquite. On Kindle
the token is `/var/local/potion/token` (mode 0600); on the simulator state is
kept in `.potion-simulator/`. Both locations are ignored by Git.

Notion mathematics is rendered to HTML inside `potiond` by native Rust
`katex-rs` 0.2.4. Mesquite loads the matching KaTeX 0.16.25 CSS and the
Mesquite-verified WOFF fonts, but no KaTeX JavaScript and no TeX parser.
Repeated expressions use a bounded native
LRU cache, and equations continue to scale with reader text because the result
uses KaTeX's relative HTML/CSS sizing. Nothing extra is installed on Kindle.

## Connect Notion

Notion does not provide a supported username/password login API. Obtain a
Notion access token on a computer. The easiest option for an individual user is
a [personal access token](https://developers.notion.com/guides/get-started/personal-access-tokens).
An internal integration token from <https://www.notion.so/profile/integrations>
also works, but the pages must be shared with that integration.

To connect without typing the token on a Kindle:

1. Copy the Potion installation to `/mnt/us/potion` as usual.
2. On the computer, create `/mnt/us/potion/notion-token.txt` containing only
   the token. Do not add `Bearer`, quotes, a label, or any other text.
3. Safely eject the Kindle and launch Potion from the Library or KUAL.

Potion validates the token with Notion, stores it privately as
`/var/local/potion/token` with mode 0600, and removes `notion-token.txt` after a
successful import. If validation fails, the file is left in place so it can be
replaced from a computer, and Potion displays the error on its connection
screen. A new valid `notion-token.txt` replaces an existing saved token, which
also provides a simple token-rotation path.

The USB copy is exposed to any computer connected to the Kindle until it is
successfully imported, so do not leave it there. `user_pass.txt` is
intentionally ignored and is not read by Potion.

## Develop with CLion on macOS or Linux

Open this directory as the CLion project. Add or edit a local CMake profile and
set this CMake option:

    -DPOTION_BUILD_SIMULATOR=ON

Build/run the `potion_simulator` target. It starts `potiond`, opens the default
browser, and keeps login/settings state between runs. The wrapper defaults to
Kindle Oasis 8th generation and provides a device selector plus physical-page
button emulation. Stop the target to stop the daemon.

Command-line equivalent:

    cmake -S . -B cmake-build-debug -DPOTION_BUILD_SIMULATOR=ON
    cmake --build cmake-build-debug --target potion_simulator

For debugging, append `?page=PAGE_ID` to Potion's URL to open that Notion page
directly. The packaged Kindle launcher accepts the same page ID as its optional
first argument:

    /mnt/us/potion/potion.sh 3c5d2870a15280b48d7fe83c9f24b96e

Normal Library and KUAL launches still open the main Pages view.

## Build and deploy to Kindle

Both Potion and AnkINK use the same `kindle-dev-builder:local` Docker image and
the same cache volume, while retaining independent Dockerfiles and scripts.
Potion's build cross-compiles its small Rust renderer as an ARMv7 static
library and links it into the C++ `potiond` executable.

    ./build_on_docker.sh
    ./push_over_ssh.sh root@192.168.15.244

The build produces a USB-root layout:

```text
dist/
├── potion/                 # /mnt/us/potion
├── extensions/
│   └── Potion/             # /mnt/us/extensions/Potion (KUAL)
└── documents/
    └── Potion.sh           # /mnt/us/documents/Potion.sh (Library)
```

For a user release, archive the **contents** of `dist/`, preserving those three
top-level directories. After extracting the archive on a computer, the user
copies `potion`, `extensions`, and `documents` to the top level of the mounted
Kindle USB drive and safely ejects it. Copying the enclosing `dist` directory is
incorrect. Potion can then be launched either from its Library item or KUAL.
The direct Library item requires PEKI, the same script-launcher support used by
a Library-installed `KUAL.sh`.

For example, after building:

```sh
tar -C dist -czf Potion-kindle.tar.gz potion extensions documents
```

`push_over_ssh.sh` is the developer deployment path. It installs all three
components at their `/mnt/us` locations; it is not required for normal users.

## License and source

Potion is Copyright (C) 2026 Potion contributors and is free software licensed
under the [GNU Affero General Public License v3.0 or later](LICENSE). The
official corresponding source is this repository; every binary release must
identify its exact source tag or commit and provide equivalent access to it.
See [SOURCE.md](SOURCE.md) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
for the source and license details of bundled dependencies.

Potion is an independent project and is not affiliated with or endorsed by
Notion Labs, Inc. “Notion” is used only to identify service compatibility.
