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
build scripts, cross-compiles its Rust renderer and C++ daemon separately for
ARMEL and ARMHF, and packages both runtimes. The build container is short-lived;
the source is mounted read-only and `dist/` receives the packaged output.

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
│   ├── armel/{bin,lib}/
│   ├── armhf/{bin,lib}/
│   ├── share/potion/
│   └── potion.sh
├── extensions/Potion/
└── documents/Potion.sh
```

For a release archive, archive the **contents** of `dist/`:

```sh
tar -C dist -czf Potion-kindle.tar.gz potion extensions documents
```

The default build creates both runtimes. `KINDLE_ABI=armel` or
`KINDLE_ABI=armhf` requests a single-ABI developer build. Native dependencies,
CMake builds, package staging, and Rust target outputs remain isolated by ABI.

`scripts/validate-kindle-runtime.sh` validates ELF machine/float ABI,
interpreter and shared-library closure, then uses each packaged loader to run
`potiond --help` under QEMU user mode. A deeper advisory smoke test starts the
Rust/C++ daemon and requests `/api/status`. The launcher has an internal test
override for both ABI paths. None of these checks emulate Kindle firmware.
Potion has also been tested on an ARMHF Kindle Oasis 3, but other KindleHF
models and firmware-specific Mesquite, input, e-ink, and suspend/resume behavior
still require appropriate physical-device testing.

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

## Kindle optimization validation (2026-10-01)

The universal Kindle build uses **Release with explicit `-O2 -DNDEBUG`** for
C/C++, checked CMake IPO for Release targets, and Rust `opt-level = 2`,
`lto = "thin"`, `codegen-units = 1`. GCC flags are
`-march=armv7-a -mtune=generic-armv7-a -mfpu=neon`, with `-mfloat-abi=softfp`
for ARMEL and `-mfloat-abi=hard` for ARMHF. There is still exactly one runtime
per ABI, with the existing loaders and launcher selection. No device-specific
`-mcpu`, runtime dispatch, or `-ffast-math` was added.
OpenSSL/curl now build with fixed `-O2` and the same generic ARM flags;
the dependency-input fingerprint rebuilds the Docker image for these changes.
CMake and Cargo caches additionally separate optimization levels, Rust NEON,
and IPO by profile. Explicit flags replace cached CMake optimization flags;
source-content fingerprints invalidate C++ objects even after rsync preserves
old mtimes. Cargo tracks target flags and release profile changes. Kindle
builds clear inherited global Rust flags so the selected target profile wins.

Five developer configurations were compared: the original MinSizeRel/GCC `-Os`
and Rust `"s"` baseline; O2/Rust2 with and without Rust NEON; and O3/Rust3 with
and without Rust NEON. IPO is enabled in all four performance candidates.
The default keeps **O2/Rust2 and no additional Rust target features**: O3 and
Rust NEON did not improve the tested workloads consistently enough to justify
choosing them for the broad device range. C/C++ NEON remains enabled.

Rust NEON was tested, rather than inferred from compiler flags. ARMHF uses
`-C target-feature=+neon`. On the pinned Rust 1.92 ARMEL target, `+neon` alone
crashes LLVM during the math static-library build ("Do not know how to soften
this operator's operand!"). The working experiment uses
`-C target-feature=-soft-float,+vfp3,+neon`; the target's soft calling convention
is retained. Rust's [pinned ABI feature checks](https://github.com/rust-lang/rust/blob/1.92.0/compiler/rustc_target/src/target_features.rs)
document this ARM softfp equivalent. These experimental features generate
compiler instability warnings, another reason to require measured benefit.
Disassembly of `katex::build_html::build_html` contains NEON `vld1.32` and
`vst1.32` instructions. `scripts/check-rust-float-abi.sh` checks ELF VFP argument
attributes and C-to-Rust/Rust-to-C f32/f64 calls under both Cortex-A8 and
Cortex-A9 QEMU CPUs for each ABI; the Docker build runs this automatically.

Measurements use three runs of each configuration, reporting the median of
per-run medians in milliseconds. Standalone benchmarks call production code
with generated, disposable fixtures, without credentials or remote HTTP.
All benchmark configurations use the same rebuilt O2 native dependency SDK;
the baseline uses frozen original Rust archives and original C++ flags.
Daemon size comparisons use the actual original and candidate distributions.
QEMU timing is diagnostic, with shared-host scheduling noise; it is not a
prediction of Kindle latency. Physical ARMEL measurements use an Oasis 1,
firmware 5.16.2.1.1, temporarily held at 996 MHz for fair comparison; its
`ondemand` governor is restored afterward. ARMHF performance was measured
under QEMU, not on physical ARMHF hardware. Backend HTML generation does not
measure Mesquite painting, network latency, or panel refresh.

The workload generates a 40-section Markdown page, parses/serializes an API-like
page JSON response, renders 48 distinct fraction/radical/sum/matrix formulas,
registers 24 images, verifies a 256 KiB session-image-cache write/read, builds
and retrieves a 500-page session snapshot, and constructs the real HTTP server
with update checks disabled. HTTP server construction is not full process-to-
HTTP-ready startup. Image work covers registry/cache I/O, not network download
or browser decoding. All session caches are disposable.

Physical Oasis 1 (ARMEL):

| Workload (ms) | Baseline | O2/Rust2 | O2/Rust2 + Rust NEON | O3/Rust3 + Rust NEON | O3/Rust3 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Markdown page | 15.559 | 13.272 | 13.475 | 13.596 | 14.208 |
| Page JSON → HTML → JSON | 20.799 | 17.019 | 17.168 | 17.419 | 18.000 |
| Cold math | 3.106 | 2.882 | 2.767 | 2.885 | 2.834 |
| Markdown + math + 24 images | 17.490 | 14.956 | 15.224 | 15.427 | 15.957 |
| 256 KiB image cache write/read | 36.326 | 56.987 | 55.934 | 37.680 | 40.691 |
| 500-page snapshot build | 8.265 | 6.734 | 7.045 | 6.741 | 6.510 |
| 500-page snapshot cached | 0.375 | 0.360 | 0.386 | 0.321 | 0.321 |
| HTTP server construction | 0.095 | 0.085 | 0.085 | 0.086 | 0.085 |

ARMHF under Cortex-A8 QEMU (diagnostic):

| Workload (ms) | Baseline | O2/Rust2 | O2/Rust2 + Rust NEON | O3/Rust3 + Rust NEON | O3/Rust3 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Markdown page | 12.978 | 10.794 | 10.303 | 10.722 | 10.405 |
| Page JSON → HTML → JSON | 15.038 | 12.447 | 11.928 | 12.396 | 12.238 |
| Cold math | 1.155 | 0.895 | 0.940 | 0.901 | 0.865 |
| Markdown + math + 24 images | 14.412 | 11.659 | 11.531 | 11.531 | 11.623 |
| 256 KiB image cache write/read | 0.258 | 0.252 | 0.249 | 0.256 | 0.253 |
| 500-page snapshot build | 4.136 | 3.325 | 3.364 | 3.405 | 3.412 |
| 500-page snapshot cached | 0.227 | 0.172 | 0.176 | 0.144 | 0.146 |
| HTTP server construction | 0.035 | 0.030 | 0.030 | 0.029 | 0.030 |

Stripped packaged daemon sizes (bytes):

| Configuration | ARMEL | ARMHF |
| --- | ---: | ---: |
| Baseline | 4,742,048 | 3,890,072 |
| O2/Rust2 | 5,200,616 | 4,348,640 |
| O2/Rust2 + Rust NEON | 5,200,616 | 4,283,104 |
| O3/Rust3 + Rust NEON | 5,266,136 | 4,348,624 |
| O3/Rust3 | 5,331,672 | 4,414,160 |

Raw median/p95/sample-count records, including ARMEL QEMU, are in
[`tests/benchmark_results/2026-10-01.csv`](tests/benchmark_results/2026-10-01.csv).

Validation passed the existing three host CTest suites for this project in both
the original build and Release/O2 with IPO, JavaScript syntax checks, and all
ARMEL/ARMHF ELF, packaged-loader, dependency-closure, launcher-selection, and
QEMU `/api/status` checks for baseline and all candidates. The final default
universal package passes them again, including the new float ABI probes.
GCC reports vectorized blocks in production code (AnkINK HTTP handling and
Potion image-cache metadata). Math/card HTML from all 30 QEMU runs and 15
physical runs per project matches baseline after sorting unique inline-style
declarations and HTML attributes; text, values, and structure remain exact.
Randomized Rust map iteration makes byte hashes of HTML unsuitable for this
comparison. No installed application, account data, telemetry, or releases
were changed by these tests.

To reproduce candidate builds with the normal build entry point:

```sh
# Chosen default: O2 C/C++, Rust2, checked IPO, original Rust target features.
bash build_on_docker.sh user@host
# O3 comparison, without changing ABI or adding runtime variants:
KINDLE_CPP_OPT_LEVEL=3 KINDLE_RUST_OPT_LEVEL=3 bash build_on_docker.sh user@host
# Optional experimental Rust SIMD and GCC vector reports:
KINDLE_RUST_NEON=1 KINDLE_VECTOR_REPORT=ON bash build_on_docker.sh user@host
```

`KINDLE_IPO=OFF` allows a developer IPO comparison. The scripts accept only
optimization levels 2/3, NEON 0/1, and IPO/report ON/OFF. Each command packages
one chosen implementation per ABI, not all benchmark candidates.
Enable the optional CMake `POTION_BUILD_BENCHMARKS=ON` in a separate build
configured with the same toolchain, flags, IPO, and imported Rust archive; build
the `workload_benchmark` target. It is never copied into `dist/`. Run through
the matching packaged loader and library path, on hardware or under
`qemu-arm -r 3.0.35 -cpu cortex-a8`. Preserve each candidate's archive and
binary before changing profiles; use a fresh private fixture directory per
process. Keep timing runs serial and compare repeated results, not one noisy
sample. Avoid interpreting cache/fsync p95 spikes as compiler improvements.

Pass a fresh disposable directory to `workload_benchmark`; it writes temporary
image/page caches and `math.html`. The four Rust math unit tests also pass in
Release. O2 improves physical page JSON/rendering 18.2% and mixed Markdown/
math/images 14.5%; O3 without Rust NEON is slower on both. O2 Rust NEON improves
physical cold math about 4%, but slightly regresses page rendering and regresses
ARMHF QEMU cold math about 5%; it stays experimental. The selected daemon
grows 9.7% ARMEL / 11.8% ARMHF over baseline. Cache write timings have large
filesystem tails and no repeatable optimization benefit; cached metadata and
HTTP-server construction are small parts of overall startup.
