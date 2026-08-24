# Potion

Potion is a read-only Notion client for jailbroken Kindles. A C++17 daemon
uses Notion's official API, converts Notion-flavored Markdown to sanitized
HTML, and serves a deliberately lightweight interface to Amazon Mesquite.

Potion has no Notion write operations. It can validate a connection token,
search accessible pages, retrieve page metadata/Markdown, and fetch images.
The reader supports navigable child pages, parent-page Back navigation, regular
toggles, toggle headings, and Notion text/background highlights. Night mode can
preserve page colors, adapt them for a dark background, or also invert images.
Authentication and settings are stored by `potiond`, not Mesquite. On Kindle
the token is `/var/local/potion/token` (mode 0600); on the simulator state is
kept in `.potion-simulator/`. Both locations are ignored by Git.

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
3. Safely eject the Kindle and launch Potion from KUAL.

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

## Build and deploy to Kindle

Both Potion and AnkINK use the same `kindle-dev-builder:local` Docker image and
the same cache volume, while retaining independent Dockerfiles and scripts.

    ./build_on_docker.sh
    ./push_over_ssh.sh root@192.168.15.244

The build produces `dist/potion/` for `/mnt/us/potion` and
`dist/extensions/Potion/` for `/mnt/us/extensions/Potion`. Launch it from KUAL.
