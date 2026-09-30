![Potion](logo/potion_logo_orig_size.png)

# Potion

Potion is a lightweight Notion reader optimized for Kindle and
e-ink hardware. It turns Notion pages into a compact reading interface designed
for fast navigation, responsive controls, low memory use, and minimal
background activity on constrained devices.

Potion is primarily a reader. It intentionally provides only narrow editing
support: selected text in supported blocks can be highlighted, bolded, or
underlined, but Potion is not a general Notion editor.

**Compatibility notice:** Potion supports both older ARMEL Kindle firmware and
newer ARMHF/KindleHF firmware. It has been tested on an **8th-generation Kindle
Oasis** using ARMEL and a **10th-generation Kindle Oasis (Oasis 3)** using
ARMHF. Other Kindle models remain less well tested, so feedback is welcome.

## Features

- Search, sort, refresh, and navigate the Notion pages accessible to the
  configured connection.
- Child-page links, page history and Back navigation, direct return to the
  Pages screen, and saved reading positions.
- Paragraphs, headings, numbered and bulleted lists, toggles and toggle
  headings, tables, code, Notion text/background colors, and other common
  reading content.
- Lazy-loaded images with tap-to-expand viewing and a bounded session cache.
- Native inline and display mathematics with bundled KaTeX fonts.
- Long-press text selection and highlight, bold, underline, or clear-formatting
  actions where the selected Notion block and content type support them.
- Reader font and size controls, word and line spacing, separate code sizing,
  an experimental Bionic Reading presentation, orientation control, and
  configurable night modes.
- Physical Kindle page-button scrolling where the device and firmware provide
  those buttons. Direction can be reversed in Settings.
- Manual full e-ink refresh control.

Potion avoids persistent copies of Notion page content. The accessible Pages
listing and successfully downloaded images may be cached only for the current
Potion session to improve responsiveness and reduce repeated network use; that
temporary data is cleared when Potion restarts or the user logs out.

## Platform support

Current releases contain both Kindle ARM userspace ABIs: ARMEL for older
firmware (generally before 5.16.3) and ARMHF/KindleHF for newer firmware
(generally 5.16.3 and later). The launcher detects the installed firmware ABI
and selects the matching runtime; users install one universal package and do
not need to determine their ABI.

ARMEL has been tested on an 8th-generation Kindle Oasis, and ARMHF has been
tested on a 10th-generation Kindle Oasis (Oasis 3). The build also validates
both ABIs' ELF format, loader, packaged shared-library closure, Rust/C++
integration, and QEMU user-mode startup. QEMU does not emulate Kindle firmware
and does not validate Mesquite, Amazon application services, touch/page
buttons, or e-ink behavior; other physical models remain less well tested.

The simulator includes profiles for many Kindle screen sizes, but a simulator
profile is not a claim that the corresponding physical model has been tested.
There is not yet a comprehensive per-model compatibility matrix. Other e-ink
platforms are not currently supported; support for additional e-ink devices is
intended in the future.

## Installation over USB

Download and extract `Potion-<version>-kindle-universal.tar.gz` (or `.zip`) on
a computer. The archive is laid out like the root of the Kindle USB drive.
Copy the archive's **contents** to the top level of the mounted Kindle drive:

```text
potion/                  -> /mnt/us/potion
extensions/Potion/       -> /mnt/us/extensions/Potion
documents/Potion.sh      -> /mnt/us/documents/Potion.sh
```

Do not copy an enclosing release or `dist` directory. Safely eject the Kindle,
then launch Potion from the Library or from KUAL. The direct Library entry
requires PEKI, the same script-launcher support used by a Library-installed
`KUAL.sh`; KUAL remains an optional launch route.

## Connect Notion

Potion uses Notion's supported token-based API; it cannot sign in with a Notion
email address and password. Obtain a
[personal access token](https://developers.notion.com/guides/get-started/personal-access-tokens)
or an internal integration token, grant it the required content capabilities,
and make the desired pages accessible to it. Update-content access is required
only for Potion's limited text-formatting actions.

The token can be entered on the Kindle. While Potion is disconnected, it can
also show a temporary same-network setup URL so the token can be pasted from a
phone or computer; that setup page closes after a valid connection is stored.

To import a token through USB instead:

1. Create `/mnt/us/potion/notion-token.txt` on the mounted Kindle drive.
2. Put only the token in the file: no `Bearer`, quotes, label, or extra lines.
3. Safely eject the Kindle and launch Potion.

Potion validates the token, stores it privately, and removes the USB copy after
a successful import. If validation fails, the file remains so it can be
replaced. A new valid import file can also replace an existing saved token.

## Using Potion

The Pages screen shows the content available to the configured connection. Use
Search to filter it, choose the desired sort order, or explicitly Refresh the
session snapshot from Notion. Open a page to read it; child-page links continue
within Potion, Back follows page history, and Pages returns directly to the
listing.

Tap images to expand them. Open toggles in place, and use the appearance panel
to adjust font, size, spacing, code size, and Bionic Reading. Long-press and
drag to select text; the formatting menu is available only where Potion can
safely map the selection back to editable Notion content.

## Security and privacy

The saved token is stored outside the USB-visible filesystem at
`/var/local/potion/token` with mode 0600. The temporary USB import file is
visible to any connected computer until it is successfully imported, so do not
leave it on the device. Logging out removes the saved token and clears Potion's
session caches.

The normal application API listens only on the Kindle loopback interface. The
temporary network setup page exists only while Potion has no valid token and is
closed after connection. Potion fetches only pages shared with the configured
Notion connection and sanitizes the HTML it renders.

## Technical characteristics

Potion uses the Kindle's built-in Mesquite browser for display and a compact
C++17 daemon to access Notion's API and produce sanitized HTML. Mathematics is
rendered natively before the page reaches the browser; the Kindle does not run
KaTeX JavaScript or install a TeX engine. Images remain lazy in the interface,
and page responses do not wait for all images to download.

## Building from source

With Docker installed and running locally:

```sh
./build_on_docker.sh
```

To build through an SSH-accessible machine that has Docker:

```sh
./build_on_docker.sh user@host
```

The remote form synchronizes the working tree, runs the same Docker build on
the remote machine, and returns the completed artifacts to the local `dist/`
directory. The default build produces both native ABIs in one USB-ready
package. In either mode, `dist/` contains the `potion`, `extensions`, and
`documents` entries.

## Deploy over SSH

If the Kindle has SSH access, for example through USBNetwork, the built project
can optionally be installed with:

```sh
bash push_over_ssh.sh root@device_ip
```

This installs the three components under `/mnt/us` but does not relaunch the
application. Normal users do not need SSH and can use the USB installation
method above.

For simulator setup, development environment configuration, architecture
details, cross-compilation internals, and debugging workflows, see
[DEVELOPMENT.md](DEVELOPMENT.md).

## Support Potion

Potion is developed independently and provided as free and open-source software.

Development and real-device testing are currently centered on an **8th-generation Kindle Oasis**, which is the Kindle hardware I currently have available. If you find Potion useful, you can [sponsor the project on GitHub](https://github.com/sponsors/nimamox).

Your support can help fund the purchase of newer Kindle models and other e-ink devices for real-device testing. This would make it possible to investigate compatibility issues, validate Potion on a wider range of hardware, and gradually add support for additional devices and platforms.

Sponsorship is entirely optional. Potion remains free and open-source software.

## Project status and license

Potion is an independent, focused 0.x Notion reader under active development.
It is not affiliated with or endorsed by Notion Labs, Inc. “Notion” is used
only to identify service compatibility.

Potion is Copyright (C) 2026 Potion contributors and is free software licensed
under the [GNU Affero General Public License v3.0 or later](LICENSE). Binary
releases and redistributions must identify their exact corresponding source and
preserve the applicable notices. See [SOURCE.md](SOURCE.md) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
