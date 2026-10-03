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
| Boost | 1.89.0, constrained to `<1.90.0` | Test public 1.91.0 and recheck the Cobalt metadata failure before changing the range. Upstream 1.92.0 is not yet present in the queried remote. |
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
