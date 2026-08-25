# Corresponding source

Official Potion binaries are built from the tagged source at
<https://github.com/nimamox/Potion_Kindle>. The repository includes the Docker
build definition, cross-build scripts, packaging scripts, UI assets, and all
Potion source needed to reproduce the Kindle executable and distribution.

Each binary GitHub release must identify its exact source tag or commit and
provide equivalent, no-charge access to that source. The pinned third-party
versions and their source locations are recorded in `THIRD_PARTY_NOTICES.md`.
The GNU runtime and certificate packages are unmodified Debian builds; their
exact package versions and Debian source locations are recorded there as well.
The native math renderer's exact Rust dependency resolution is recorded in
`backend/math/Cargo.lock`; Cargo obtains crate sources from the registry
locations recorded by that lockfile.

Anyone redistributing the binary is responsible for preserving the license and
notices and for satisfying the source-delivery requirements of AGPL-3.0-or-later
and the applicable third-party licenses.
