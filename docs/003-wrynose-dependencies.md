# Wrynose dependency qualification

This work accompanies the Yocto 6.0.3 pilot. SDK and Conan publication remain
deferred until the toolchains and their consumers have been qualified.

## Distribution contract

The library exposed to consumers must use unmodified public Conan Center
recipes. A locally modified recipe under a public name does not satisfy this
requirement. Validation uses an isolated Conan home with only
`https://center2.conan.io` as its dependency remote and records recipe revisions.
The locally produced Yocto SDK is a build tool, not a library dependency.

Compiled distribution packages should use the same public recipes. A private
override is permitted only for a reproduced packaging limitation, with the
affected target, minimal delta and removal criterion recorded. In particular,
the existing Boost and ncurses overrides must be re-evaluated before reuse.

## Inventory, 2026-10-03

Versions below were queried from Conan Center, not inferred from upstream
release numbers. Availability is not a claim of build or runtime validation.

| Dependency | Current public resolution | Qualification work |
| --- | --- | --- |
| Boost | Pilot candidate 1.91.0, constrained to `<1.92.0` | Native Linux static libraries/static plugins passed 51 tests and the external consumer with the unmodified public recipe. Other topologies, Windows/macOS and Yocto remain to qualify. Upstream 1.92.0 is not yet present in the queried remote. |
| OpenSSL | 3.6.5, constrained to `<4` | Qualify 3.6.5 first; assess the separate 4.0.3 major upgrade before widening compatibility. |
| Protobuf | 7.35.0 | Latest available public recipe; keep host `protoc` and target runtime aligned. |
| nlohmann_json | 3.12.0 | Validate existing public resolution with GCC 15. |
| spdlog / fmt | 1.17.0 / 12.1.0 | Validate existing public resolution with GCC 15. |
| yaml-cpp | 0.9.0 | Validate static and shared runtime combinations. |
| libmaxminddb | 1.12.2 | Validate public recipe on both target architectures and libcs. |
| ncurses | 6.5 | Replace the vendored packaging recipe if public builds and WebTTY tests pass. |
| docopt.cpp | 0.6.3 | Latest available public recipe; retain Windows Boost.Regex qualification. |
| LibreSSL (optional provider) | 3.9.1 | Latest available public recipe; retain the alternate-provider contract. |

`external/gtest/CMakeLists.txt` contains an old 1.12.1 fallback but is not
included by the build. The active tests use the repository's own test harness.
Do not introduce a new GoogleTest dependency merely to update this unused file.

## Acceptance sequence

1. Record public graphs and recipe revisions before the build. Check the public
   dependency policy without local dependency exports.
2. Reproduce or dismiss the Boost version cap on a native Linux build with
   strict warnings, tests and an external package consumer.
3. Build against the four local SDKs: x86_64 and ARM64 targets, each with musl
   and glibc. Start packaging with public dependencies.
4. Remove obsolete overrides only after their affected target tests pass.
   Keep a documented reproduction for every remaining override.
5. Qualify the library/plugin linkage matrix and supported native platforms
   specified in `001-sdk-engineering.md`, plus the relevant sanitizers and
   runtime tests. Report unexecuted configurations explicitly.
6. Record selected versions, recipe revisions, build duration and package sizes
   with the validation evidence. Update CI before considering publication.

The machine-local progress matrix and build logs live under
`../.yocto-pilot/` relative to the repository. A successful dependency graph
resolution alone does not qualify a dependency update.

## Cross-built consumer execution

The package consumer is registered with CTest using its executable target, so
`CMAKE_CROSSCOMPILING_EMULATOR` can supply a runner when needed. Conan runs it
only when `can_run()` permits execution. The Yocto profile disables execution
by default; qualification must explicitly set
`tools.build.cross_building:can_run=True` after provisioning the runner.

An x86_64 fully static musl consumer can run directly on the Linux host. ARM64
requires QEMU or an ARM64 execution host. A glibc consumer may require the SDK's
loader and libraries when its glibc is newer than the workstation's version.
Do not mark a skipped external consumer as a runtime pass, and do not infer
old-kernel compatibility from QEMU user-mode execution on the host kernel.

## Pilot findings

The Linux and macOS native CI linkage matrices passed with public Boost 1.91.0
and the current public dependency graph. Windows qualification is still running.

On x86_64/musl, the public dependency builds now complete with GCC 15. The Yocto
tool recipe must append SDK flags to the Conan build environment: defining them
replaced flags supplied by dependency recipes, including ncurses' GCC 15 C17
compatibility flag. No public dependency recipe was changed for this fix.

The ARM64 ncurses recipe rejects cross compilation before building. The separate
`conan/recipes/ncurses-wrynose` packaging candidate narrows that exception to
Linux x86_64 -> ARM64 with a Yocto 6.0 SDK. Its musl C++/terminfo/window/input
consumer passed under QEMU. glibc and the complete ARM64 rstream package remain
to qualify. This recipe is not a library dependency default.

Fully static runtime qualification excludes the shared-module loader fixture,
which requires dynamic loading; native dynamically linked runtime matrices
retain it. The external consumer explicitly links fully statically when testing
that package option. CLI argument readers use const references to docopt values,
avoiding unnecessary variant copies exposed by GCC 15's optimized diagnostics.

## OpenSSL major-version boundary

The pilot retains the latest selected 3.x recipe (3.6.5), with the `<4` bound.
OpenSSL 4 removes the ENGINE API, as documented by
[OpenSSL](https://openssl-library.org/post/2025-12-18-remove-engines/index.html).
rstream has both provider-based PKCS#11 and a legacy ENGINE compatibility path.
Widening the bound therefore requires explicit provider/legacy configuration
and supported-Boost qualification; a successful generic TLS build alone is
insufficient. Do not silently drop that capability to claim a dependency bump.

## Reproducing the local pilot

Use Conan 2.31.2 in an isolated `CONAN_HOME`, install `conan/config`, and create
the required local `yocto-toolchain/6.0.3` tool packages from verified archives
as described in the toolchain repository. No personal publishing credentials
are needed. The pilot script currently targets a Linux x86_64 build host.

```sh
conan config install conan/config
python3 conan/qualify_yocto.py --arch x86_64 --libc musl --jobs 4
python3 conan/qualify_yocto.py --arch x86_64 --libc glibc --jobs 4
python3 conan/qualify_yocto.py --arch arm64 --libc musl --jobs 4 --arm-ncurses-exception
python3 conan/qualify_yocto.py --arch arm64 --libc glibc --jobs 4 --arm-ncurses-exception
```

Run configurations serially when they share a Conan cache: some recipes write
compiler configuration into a shared source directory. `--plan` prints the
package build command without running Conan. `--output` selects the evidence
directory; the default is `out/yocto-pilot/<architecture>-<libc>`.

The script verifies public target recipe revisions, records a locked input
graph, validates the installed SDK provenance, forces package tests and the
external consumer, and writes the final graph and lockfile. ARM64 execution
requires `qemu-aarch64`; x86_64 glibc execution uses the SDK loader and sysroot
libraries. These runs use the workstation kernel and do not certify an older
kernel. The ARM ncurses exception is explicit, packaging-only and still needs
the full matrix qualification below. No upload or publication command exists
in this script.

## Archive and website checks

The initial x86_64/musl `rstream-utils` and `rstream-webtty` archives were
extracted and all eight/two shipped executables passed `--help` and `--version`
without the SDK environment. ELF inspection confirmed fully static x86_64
executables. Both archives include the public ncurses terminfo directory.

This check exposed an old assumption in the tunnel UI: it only looked for
`share/terminfo.db`. The UI now also selects `share/terminfo`, preferring that
format while preserving an explicitly configured `TERMINFO`. Selection tests
cover both layouts, a missing database and an invalid directory. A fully static
probe initialized ncurses and used the terminfo data from the extracted archive.
Archives produced before this fix must be rebuilt; CLI help alone does not
qualify the interactive terminal UI.

The exact Hello World sources from the product page compiled and ran with both
musl SDKs (ARM64 via QEMU). The page explicitly selects musl for static linking;
the glibc SDK is validated separately with a dynamically linked C++20 program.
Before publication, refresh the changelog and retain the package API fields
`arch` (host), `targetArch`, `libc`, `version`, `checksum`, `components.host` and
`components.target`. Prefer `readelf` to host `ldd` for inspecting a foreign ELF.
The site remains unchanged while publication is deferred.
