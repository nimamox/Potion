# Potion development

This document covers host development, architecture, cross-compilation, and
developer deployment. For features, normal installation, and user-facing
security information, see [README.md](README.md).

## Architecture

Potion has an ES5 HTML/CSS interface rendered by Amazon Mesquite and a C++17
daemon that listens on `127.0.0.1:8766`:

```text
Mesquite HTML/CSS/ES5 UI -> XMLHttpRequest -> potiond -> Notion API
                                                |
                                                +-> native katex-rs renderer
```

The daemon uses Notion's supported token API, converts Notion-flavored Markdown
to sanitized HTML, proxies images, persists reader settings and positions, and
performs the limited supported formatting updates. Page contents are not
persistently cached.

The accessible-page snapshot and downloaded images use bounded session-only
storage under `/tmp/potion_cache/`. Startup and logout clear this data. Page
HTML is returned without waiting for image downloads; one conservative backend
worker can prefetch registered images into the same cache used by lazy image
requests.

Notion mathematics is rendered by `katex-rs` 0.2.4 before the page reaches
Mesquite. The UI loads matching KaTeX 0.16.25 CSS and WOFF fonts, but no KaTeX
JavaScript or browser-side TeX parser. A bounded native LRU cache reuses
expressions. Narrow, structure-specific DOM repairs handle old-Mesquite layout
defects; avoid replacing them with broad page reconstruction without real
device evidence.

The Kindle launcher marks the window Whisper-Touch capable through the firmware
window-manager utility. AwesomeWM can then deliver Oasis Page Up/Page Down
events directly to WebKit. `potiond` does not monitor `/dev/input` or expose an
input-polling endpoint.

## Host simulator

The simulator runs the real local daemon with the same reader UI and stores its
token, settings, and other private state under `.potion-simulator/`. That
directory is ignored by Git.

Configure and run from the command line:

```sh
cmake -S . -B cmake-build-debug -DPOTION_BUILD_SIMULATOR=ON
cmake --build cmake-build-debug --target potion_simulator
```

The target starts `potiond`, opens the browser wrapper, and stops the daemon
when the target is stopped. The wrapper provides viewport profiles,
portrait/landscape switching, and physical page-button emulation. These
profiles are development aids, not a physical-device compatibility matrix.

For CLion, open the repository as the project, create a local CMake profile with
this option, and run the `potion_simulator` target:

```text
-DPOTION_BUILD_SIMULATOR=ON
```

To open a page directly during debugging, append `?page=PAGE_ID` to the
simulator URL. The packaged launcher accepts the same 32-hex Notion page ID as
an optional first argument:

```sh
/mnt/us/potion/potion.sh <32-hex-page-id>
```

## CMake and native dependencies

The main options are:

- `POTION_BUILD_TESTS`: build and register native and frontend contract tests.
- `POTION_BUILD_SIMULATOR`: add the browser-based native-host simulator.
- `POTION_MATH_LIBRARY`: use a prebuilt `libpotion_math.a`; otherwise CMake
  builds the Rust math library through Cargo.

`potion_core` contains JSON handling, state and reading-position persistence,
Markdown conversion, native math integration, Notion access, session image and
Pages caches, and the HTTP server. It links libcurl, OpenSSL, threads, and the
Rust static math library.

## Docker cross-build internals

The supported release build is:

```sh
./build_on_docker.sh
```

It builds or reuses the shared `kindle-dev-builder:local` image and
`ankink-kindle-build-cache` named volume. Potion keeps its own Dockerfile and
build scripts, cross-compiles its Rust renderer as an ARM static library, and
links it into the C++ daemon. The build container is short-lived; the source is
mounted read-only and `dist/` receives the packaged output.

A remote build uses:

```sh
./build_on_docker.sh user@host
```

The local machine requires `ssh` and `rsync`; the remote requires `rsync`,
Docker, and working Docker access. The script synchronizes to the reusable
`/tmp/kindle-build-$USER/Potion` directory, excluding Git data, credentials,
`dist/`, simulator state, and local build output. It invokes the same local
Docker path remotely and copies `dist/` back only after a successful build.
Docker image layers and the named build-cache volume remain outside `/tmp` and
are reused by later builds.

The package layout is:

```text
dist/
├── potion/
├── extensions/Potion/
└── documents/Potion.sh
```

For a release archive, archive the **contents** of `dist/`:

```sh
tar -C dist -czf Potion-kindle.tar.gz potion extensions documents
```

## Kindle runtime and deployment

For an SSH-accessible development Kindle:

```sh
bash push_over_ssh.sh root@device_ip
```

The script prefers `rsync` and falls back to `scp`. It updates the app,
KUAL extension, and Library launcher, but does not restart Potion.

On launch, the script stops stale Potion UI/backend instances, starts `potiond`
on `127.0.0.1:8766`, copies the Mesquite assets to a content-versioned path
under `/var/local/mesquite`, updates `/var/local/appreg.db`, and asks
`com.lab126.appmgrd` to launch the app. The manifest suppresses Mesquite's
otherwise-empty navigation strip. The Close button also stops the backend.

The saved token and settings live under `/var/local/potion`. A one-time token
file at `/mnt/us/potion/notion-token.txt` is validated, moved to private state,
and deleted on success. When unauthenticated, Potion may also start its narrow
temporary LAN token-entry endpoint; it is stopped once authentication succeeds.

## Tests and debugging

A normal native validation cycle is:

```sh
node --check assets/app.js
cmake --build cmake-build-debug -j4
ctest --test-dir cmake-build-debug --output-on-failure
```

The simulator is the fastest correctness check, but old Mesquite behavior,
e-ink refreshes, selection dragging, physical buttons, font metrics, images,
and native math layout should be verified on a real Kindle. Runtime logs are
written to `potiond.log` in the installed app directory. Useful loopback
diagnostics while Potion is running include:

```sh
wget -qO- http://127.0.0.1:8766/api/status
wget -qO- http://127.0.0.1:8766/api/pages
```

## Source and dependency compliance

The exact official source and redistribution obligations are documented in
[SOURCE.md](SOURCE.md), [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), and
[LICENSE](LICENSE). Potion's reproducible Fast Font generation and verification
details are documented in [SOURCE.md](SOURCE.md); keep the notices and license
with every distribution.
