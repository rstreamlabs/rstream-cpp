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
The upgrade scope covers all direct, transitive, build and active test
dependencies, not only Boost. Record why any selected version remains behind
the newest public recipe; do not modify public dependency recipes to force it.

| Dependency | Current public resolution | Qualification work |
| --- | --- | --- |
| Boost | Pilot candidate 1.91.0, constrained to `<1.92.0` | Final native Linux and all four Yocto cells passed with the unmodified public recipe; native CI linkage matrices are tracked below. Upstream 1.92.0 is not yet present in the queried remote. |
| OpenSSL | 4.0.3, constrained to `<5` | Final native and all four Yocto cells passed with 4.0.3. The separate native 3.6.5 compatibility suite and external consumer also passed. |
| Protobuf | 7.35.0 | Latest available public recipe; host `protoc` and target runtime are aligned in the passing final graphs. |
| nlohmann_json | 3.12.0 | Passed the final native and GCC 15 cross matrix. |
| spdlog / fmt | 1.17.0 / 12.1.0 | fmt 12.2.0 is available, but the unchanged spdlog 1.17.0 recipe pins 12.1.0. Retain that supported pair during the pilot. |
| yaml-cpp | 0.9.0 | Passed the local matrix; shared library/plugin configurations are also checked by native CI. |
| libmaxminddb | 1.12.2 | Public recipe passed on both target architectures and libcs. |
| ncurses | 6.5 | Public recipe used for x86_64-host/x86_64 targets; scoped ARM host/target packaging exceptions and their validation status are documented below. |
| docopt.cpp | 0.6.3 | Latest available public recipe; retain Windows Boost.Regex qualification. |
| LibreSSL (optional provider) | 3.9.1 | Latest available public recipe; retain the alternate-provider contract. |
| Abseil | 20260107.1 | 20260526.0 is available, but the unchanged Protobuf 7.35.0 recipe caps its dependency at 20260107.1. Keep compiler and runtime graphs compatible. |
| zlib | 1.3.2 | Latest available public recipe in the selected graph. |
| b2 / CMake / Meson / Ninja | 5.5.3 / 4.4.3 / 1.10.2 / 1.13.2 | Latest available public recipes; the Yocto SDK also supplies its own build tools. |
| pkgconf | 2.0.3 | 2.5.1 is available; the unchanged ncurses recipe pins this build tool to 2.0.3. This is not a shipped runtime library. |
| bzip2 | 1.0.8 | Latest available public recipe; used by the unrestricted native Boost graph. |
| libiconv (macOS) | 1.17 | 1.18 is available, but the unmodified Boost 1.91 recipe pins 1.17. |
| NASM / Strawberry Perl (Windows) | 3.01 / 5.40.2.1 | The Windows dependency-refresh build passed on `bc38d15` with these public versions. Compatibility variants also retain upstream-pinned Perl 5.32.1.1. Final product-source CI remains a separate gate. |
| jom (Windows) | 1.1.4 | Public OpenSSL 4 build tool passed the Windows dependency-refresh build on `bc38d15`. |
| libbacktrace | cci.20210118 | cci.20240730 is available; the unchanged Boost recipe pins cci.20210118 when stacktrace support is enabled. |

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

The first complete native CI run passed the Linux, macOS and Windows linkage
matrices, including the Windows ASan gate and external consumers, with public
Boost 1.91.0. Subsequent source changes require their own CI results.

All four SDK archives passed installation and a C++20 execution smoke test;
ARM64 used QEMU. The x86_64/musl package including the terminfo fix passed 51
tests and its external consumer. ARM64/musl passed the earlier 50-test baseline.
Both glibc packages passed 52 tests and their external consumers (ARM64 under
QEMU). These baseline runs used OpenSSL 3.6.5; the final matrix must repeat with
OpenSSL 4.0.3 and the final source revision.

Python-wrapped CLI startup tests now receive the target's cross runner, including
the SDK loader arguments. Native and static cross builds omit that prefix.
Regression tests inspect generated CTest commands for all three cases.

On x86_64/musl, the public dependency builds now complete with GCC 15. The Yocto
tool recipe must append SDK flags to the Conan build environment: defining them
replaced flags supplied by dependency recipes, including ncurses' GCC 15 C17
compatibility flag. No public dependency recipe was changed for this fix.

The ARM64 ncurses recipe rejects cross compilation before building. The separate
`conan/recipes/ncurses-wrynose` packaging candidate narrows that exception to
Linux x86_64 -> ARM64 with a Yocto 6.0 SDK. Its C++/terminfo/window/input consumers
passed under QEMU on both libcs, as did the complete baseline rstream packages.
This recipe is not a library dependency default. Its export now excludes
generated consumer CMake files so running a test cannot change the recipe revision.

Fully static runtime qualification excludes the shared-module loader fixture,
which requires dynamic loading; native dynamically linked runtime matrices
retain it. The external consumer explicitly links fully statically when testing
that package option. CLI argument readers use const references to docopt values,
avoiding unnecessary variant copies exposed by GCC 15's optimized diagnostics.

## OpenSSL major-version boundary

The public range accepts OpenSSL 3 and 4, and currently resolves to 4.0.3.
OpenSSL 4 removes the ENGINE API, as documented by
[OpenSSL](https://openssl-library.org/post/2025-12-18-remove-engines/index.html).
rstream has both provider-based PKCS#11 and a legacy ENGINE compatibility path.
Legacy ENGINE users must select OpenSSL 3; OpenSSL 4 uses the provider path for
PKCS#11. A generic TLS test does not qualify an external PKCS#11 provider or HSM.
Unsupported explicit ENGINE configurations fail; they do not fall back to an
unauthenticated connection. Hardware/provider integration remains an explicit
qualification item before claiming that integration is supported with OpenSSL 4.

The native 4.0.3 candidate exposed the deprecated `SSL_set1_host` call. The
OpenSSL 4 path now uses `SSL_set1_dnsname`, retaining the existing API for
OpenSSL 3 and LibreSSL. TLS tests check accepted and rejected DNS, IPv4 and
IPv6 certificate identities. Its native suite and external consumer passed;
cross-platform and cross-compilation qualification must still complete.
The DNS API follows the [OpenSSL 4 documentation](https://docs.openssl.org/4.0/man3/SSL_set1_host/).

The LibreSSL candidate exposed OpenSSL ENGINE sources being compiled despite
the capability being disabled, and PKCS#11-only helpers being compiled with
that feature disabled. Those sources/helpers now follow their capability
guards. Unsupported PKCS#11 configurations still report an error; qualification
does not add that feature to LibreSSL.

LibreSSL also rejected literal IP addresses sent as SNI. The client now sends
SNI only for DNS names, as required by
[RFC 6066 section 3](https://www.rfc-editor.org/rfc/rfc6066.html#section-3),
while retaining certificate verification for IP identities. An explicitly empty
SNI still verifies the endpoint identity. Tests accept matching IP certificates
and reject a DNS-only certificate for that case. Provider-neutral group tests
use `P-256`; LibreSSL does not recognize the OpenSSL alias `secp256r1`. Requesting
unsupported raw public keys now fails explicitly instead of ignoring the request.

CI repeats the Linux static consumer with OpenSSL 3.6.5 and LibreSSL 3.9.1 and
the Windows consumer with explicitly selected public Boost 1.83. The default
native matrix targets OpenSSL 4 and Boost 1.91. A consumer-selected Boost
version is forced at the root so a nested Windows docopt requirement cannot
silently select a different version.

## Reproducing the local pilot

Use Conan 2.31.2 in an isolated `CONAN_HOME`, install `conan/config`, and create
the required local `yocto-toolchain/6.0.3` tool packages from verified archives
as described in the toolchain repository. No personal publishing credentials
are needed. The pilot script defaults to a Linux x86_64 SDK host.

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
directory; the default is `out/yocto-pilot/<sdk-version>/<architecture>-<libc>`
so Scarthgap and Wrynose results do not overwrite one another.

To qualify the library and its external consumer without the command-line tools:

```sh
python3 conan/qualify_yocto.py --arch arm64 --libc musl --jobs 4 --library-only
```

This selects the existing `build_bins=False` and `with_ncurses=False` options.
ncurses is used by the tunnel CLI, so this library configuration has no ncurses
dependency. It retains C++ tests, strict warnings and public-recipe integrity
checks, omits distribution build identity, and rejects
`--arm-ncurses-exception`. Recipe defaults are unchanged. Library evidence uses
a separate `library/` subdirectory under the version and optional SDK-host
directory. Its graph resolution must not be reported as a compiled or tested
library until the package build and external consumer actually pass.

With CLI tools disabled, CMake runs the core and I/O tests without registering
tests that depend on ncat, nperf, tunnel or webtty targets. On source `2e4c3cb`,
all four x86_64-host Wrynose public-library pilots passed: x86_64 and ARM64
targets, each with musl and glibc. Each uses 11 verified public target recipes;
musl runs 30 tests, glibc runs 31, and every configuration passes an external
consumer. ARM64 target execution uses QEMU. These library results exclude CLI
archive qualification, which is tracked separately. Exact graphs, source
revisions and test evidence are indexed in
`../.yocto-pilot/public-library-progress.json`.

The Linux static/static CI job also builds this library-only configuration from
a verified and locked public dependency graph, then runs its package and
external-consumer tests. CI initializes its Conan cache outside the checkout;
Conan source exports exclude local `.conan2` and `.ccache` directories.

The driver accepts all 18 historical SDK target names, including ARMv6/v7,
32-bit x86, both MIPS byte orders, PPC64 and RISC-V. It keeps target identities
distinct in `os.sdk` and selects the corresponding QEMU CPU and sysroot.
These additional dispatch paths still require per-target binary qualification.
LoongArch needs separate Conan architecture/recipe work; it remains part of
the modern SDK extension scope.

The driver also accepts `--arch armv7hf`, preserving the Conan hard-float
architecture and using `qemu-arm -cpu cortex-a15` with the exact SDK sysroot
for both libcs. Its opt-in private ncurses candidate is restricted to the
x86_64-host Scarthgap 5.0.10 SDK. Both ARM32 libcs pass SDK validation, the
focused ncurses consumer and the full rstream package/archive/local-engine
chain under QEMU.
Boost.Context assembly requires the SDK tuning flags through the public recipe
option `boost/*:extra_b2_flags=asmflags=-march=armv7-a asmflags=-mfpu=vfp asmflags=-mfloat-abi=hard`.
Both the production packaging command and this driver apply that option for
Scarthgap ARMv7hf. An isolated B2 build reproduces the missing-FPU failure
without it and passes a context-switching consumer with it. The public Boost
recipe is unchanged.
The public OpenSSL 4 recipe additionally requires
`-c:h openssl/*:user.openssl:target=linux-armv4` for this Conan architecture.
This OpenSSL target retains the SDK's explicit ARMv7/VFP/hard-float tuning;
the target name does not replace it with ARMv4 compiler settings. The public
OpenSSL package builds and passes the rstream ARM32/musl runtime tests.
Public library defaults are unchanged.

OpenSSL 4's public recipe only selects the Linux x86_64 and ARM64 targets
automatically. For Scarthgap 5.0.10 and Wrynose 6.0.3, the qualification driver
and packaging script share `conan/yocto_openssl_target.py` to supply the public
`user.openssl:target` configuration for 32-bit x86, ARM32, MIPS, PowerPC64 and
RISC-V. MIPS64 uses `linux64-mips64`, matching Yocto's n64 ABI; the similarly
named `linux-mips64` would select n32. SDK compiler tuning and endianness remain
in effect, and explicit packaging options can override the default target.
These 13 target-name selections were checked against the unmodified public
recipe and upstream `Configure LIST`. That control does not qualify their
actual cross builds; the per-SDK compiler and runtime tests are still required.

ARMv7hf/glibc qualification uses QEMU 11.1.2. The workstation's QEMU 8.2.2
rejects the 64-bit-time `SO_RCVTIMEO_NEW` socket option with `ENOPROTOOPT`,
causing the plain-server runtime test to fail. Place the separately verified
QEMU 11.1.2 `qemu-arm` binary first in the qualification process's `PATH`;
the driver records its absolute path. The system emulator need not be replaced.
With this runner, all 52 package tests, the external consumer, both distribution
archives and both local CE tunnel suites pass without product or test changes.
The original failed binary also passes three repeated untraced runs. A diagnostic
`QEMU_STRACE` run gets past the socket option but later fails a plaintext-rejection
check; that diagnostic run is retained as a failure, not counted as qualification.
The signed upstream QEMU source and runner hashes are recorded in local evidence
under `../.yocto-pilot/qemu-11.1.2-arm-runner/` and the investigation is indexed by
`../.yocto-pilot/armv7hf-glibc-time64-investigation.json`.

The driver also accepts `--arch x86_64_v2`, `x86_64_v3`, or `x86_64_v4`.
Each selects its exact SDK identity and distribution architecture while keeping
Conan's `arch=x86_64` setting. Native execution checks the required CPU flags
on every host core before invoking Conan. If the physical CPU does not support
the selected level, supply a separately verified runner, for example
`--runner-command '/path/to/sde64 -skx --'` for Intel SDE. The argument prefix
is parsed without a shell; glibc execution retains the selected SDK loader and
library paths. An ARM64 SDK host requires an explicit verified runner for these
x86 ISA levels. Command support and CPU checks alone do not qualify a complete
C++ package build; that requires package tests, the external consumer and
distribution runtime checks for the exact SDK.

For slow instrumentation, explicitly select `--test-timeout-scale 4
--test-timeout-seconds 300`. These configure the existing test-only deadline
and CTest limits and are recorded alongside the runner in `command.json`.
The values use Conan's typed CMake cache configuration so project defaults
cannot silently reset them; a regression control checks Conan parsing and
CMake toolchain loading together. The compiler cache defaults to `ccache/`
inside the explicitly selected `CONAN_HOME`, unless `CCACHE_DIR` is supplied.
Production timeouts and the default native qualification limits are unchanged.
The discovery test now observes the same test timeout scale as the other
runtime tests. The existing instrumented child-exit stress test reduces its
iteration count with this scale; native CI retains the full count.

For ARM64 SDK-host qualification, run inside Linux ARM64 userspace with
`--sdk-host aarch64`. Create the local tool package with Conan's `arch=armv8`
setting and the archive built for `host-aarch64`; the target remains selected
by `--arch`. Add `--host-execution emulated` when the whole host userspace runs
under QEMU. The driver checks host identity before invoking Conan, validates
the SDK provenance, and excludes the corresponding host sysroot when selecting
target libraries. ARM64-host results default to
`out/yocto-pilot/<sdk-version>/aarch64/<architecture>-<libc>`.
The target runners must be available in that environment: `qemu-aarch64` for
ARM64 targets, and `qemu-x86_64` for x86_64 targets from an ARM64 SDK host.
The Scarthgap ARM64-host/ARM64-target musl chain passes package tests, an
external consumer, archive checks and both local CE tunnel suites under
emulation. This does not qualify other host/target/libc combinations.

The Wrynose 6.0.3 ARM64-host/ARM64-target musl chain also passed with
source revision `f9f2447`: 51 package tests, one external consumer, both
distribution archives and both local CE tunnel suites. This uses the explicit
Wrynose ncurses packaging exception and emulated ARM64 host execution.
Together with the four x86_64-host pilots, five modern SDK/C++ combinations
are qualified; the remaining 71 SDK combinations still need full qualification.
Local evidence is indexed in `../.yocto-pilot/wrynose-build-matrix.json`.

The sixth modern SDK, ARM64-host/ARM64-target glibc, passed installation,
compiler identity, C++20 execution, Perl and component checks. Its first
ncurses consumer exposed a runtime isolation issue: QEMU `-L` alone still
allowed the ARM64 container's `ld.so.cache` to select its older libc.
The unchanged binary passes when the runner explicitly invokes the SDK
loader with `--inhibit-cache` and its library directories. The corrected
Conan consumer also passes against the exact existing ncurses package;
full rstream qualification for this sixth combination remains pending.
The qualification driver and both private ncurses test packages now select
that explicit loader for glibc. Missing, ambiguous or external loader paths
fail before executing tests. Static musl execution is unchanged.

The Wrynose packaging candidate also permits exactly the ARM64-host/x86_64-target
combination for 6.0.3, with either libc. Its consumer selects `qemu-x86_64`
and uses the same explicit SDK loader for glibc. The qualification command
requires `--sdk-host aarch64 --arch x86_64 --arm-ncurses-exception`;
`--library-only` continues to select only public dependencies and excludes
ncurses. The standard Intel SDKs and focused consumers now pass for both
libcs; their full rstream qualification is in progress.

The next packaging candidate additionally permits `x86_64_v2` on that same
ARM64 host and exact 6.0.3 SDK version. Its focused consumer selects QEMU
`-cpu Nehalem`; full qualification requires the explicit
`--runner-command "qemu-x86_64 -cpu Nehalem"` option. The CPU instruction
probe passes inside the pinned ARM64 container. Other SDK versions and
x86 ISA levels remain excluded from this private exception. Actual v2
SDK, consumer, package and archive qualification is still pending.

The packaging script chooses the SDK host from `uname -m` for a local build,
or from `CONAN_DOCKER_PLATFORM` for its Docker builder (default `linux/amd64`).
The build profile and a custom Compose configuration must describe that same
host. `USE_PATCHED_CONAN_DEPS=off` continues to disable all private packaging
overrides. This host selection does not alter the public library recipe.

The same driver accepts the pinned Scarthgap maintenance candidate with
`--sdk-version 5.0.10`, after creating its local tool package from a verified
archive. That path preserves GCC 13.3 and starts with public dependency recipes;
the public ncurses recipe rejects ARM cross compilation before building. An
explicit `--arm-ncurses-exception` selects the separate
`ncurses/6.5@rstream/scarthgap` packaging recipe, restricted to 5.0.10. Its
four x86_64-host SDK configurations (x86_64/ARM64 targets, musl/glibc) passed
the C++ package tests (51 for musl, 52 for glibc), external consumers, archive
checks and both local CE tunnel suites. The ARM64 ncurses consumers also passed
for both libcs. These results used the x86_64-host packaging exception;
the ARM64-host/ARM64-target musl ncurses build and focused consumer also passed
in emulated ARM64 userspace, followed by the complete rstream chain.
`build-conan-cross.sh` also selects public Boost for exactly 5.0.10; other
legacy SDK versions retain their prior settings. No SDK or package is uploaded.

As of 2026-10-04, eleven Scarthgap 5.0.10 SDK/package combinations are qualified:

| SDK host | Targets | Qualified libc |
| --- | --- | --- |
| x86_64 | x86_64, x86_64_v2, arm64, armv7hf | musl and glibc |
| x86_64 | x86_64_v3, x86_64_v4 | musl |
| aarch64, emulated | arm64 | musl |

Each passed its package tests, external library consumer, both distribution
archives and local CE tunnel suites. The v4 build uses verified Intel SDE,
explicit AVX-512 SDK checks and test timeout scale 4; this is functional
qualification under emulation, not a hardware performance result. The other
61 legacy SDK combinations and their C++ packages remain unqualified.
Local evidence is indexed in `../.yocto-pilot/scarthgap-build-matrix.json`.

Completed local SDK installations may be retired to make space for the next
builds. Their verified distribution archives and recipe backups are retained.
Reinstallation relocates an SDK to a new prefix and creates a new Conan package
revision; regenerate dependency graphs before using it again. Historical graph
paths are qualification evidence, not a promise that cache directories remain
installed. Ten legacy and four modern x86_64-host SDK installations have been
retired after archive recovery checks. Their exact recovery commands are indexed
in `../.yocto-pilot/sdk-installation-retirement-inventory.json`.
The modern recovery control re-created a removed tool requirement from its
retained installer and exact recipe, then passed C++20 compilation/execution
and the SDK Perl check in a disposable cache.

The script verifies public target recipe revisions, records a locked input
graph, validates the installed SDK provenance, forces package tests and the
external consumer, and writes the final graph and lockfile. ARM64 execution
requires `qemu-aarch64`; x86_64 glibc execution uses the SDK loader and sysroot
libraries. These runs use the workstation kernel and do not certify an older
kernel. The ARM ncurses exception is explicit and packaging-only; its final matrix
qualification is recorded below. No upload or publication command exists
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
The site deployment remains unchanged. A local documentation branch replaces
host `ldd` with architecture-independent `readelf -l` / `readelf -d` inspection,
checking for the absence of `INTERP` and `NEEDED`. The exact published sample
also passed again with both final musl SDKs; its archive hashes and source hashes
are recorded in `../.yocto-pilot/site-sample/final/validation.json`.

## Native CI dependency resolution

A restored Conan cache can resolve a widened OpenSSL range to its cached 3.x
version. The default CI build explicitly refreshes all ranged dependency and
build-tool resolutions; the separate consumer check retains the explicit 3.6.5
compatibility run.

The public Boost 1.91 recipe expects `cobalt_io_ssl`, but the macOS and Windows
packages tested by CI lacked that optional library. The root `qualification`
profile disables Cobalt for 1.91, which rstream does not use. The public recipe
and the library consumer's component choices remain unchanged. This limitation
still applies to an application that independently requires Cobalt; it must
qualify that upstream component separately. Linux's unrestricted baseline passed.

## Completed local matrix and remaining gates

Final C++ qualification uses source revision `61b9425` and the four regenerated
SDKs built from toolchain revision `17d5e9a`. The SDKs passed C++20 and direct
Perl execution. The OpenSSL 4 cross matrix passed 51 tests on each musl target,
52 on each glibc target, and an external consumer in every cell. Native Linux
also passed 52 tests plus a consumer for OpenSSL 4, OpenSSL 3, public Boost 1.83,
and LibreSSL. Exact references, locks, timings and package sizes are recorded
in `../.yocto-pilot/qualification/provider-init/summary.json`. The dependency sets
are unchanged from the prior full run, and regular package-file totals differ
by at most 296 bytes. Build times are recorded observations with cached
dependencies, not controlled performance benchmarks.

Both archive types were produced locally for all four targets. Every shipped
executable passed help/version with the relevant native/SDK/QEMU runner.
Terminfo from all eight archives passed a native ncurses data probe. The
private ARM ncurses recipe now has the same deterministic revision on both
libcs and its focused consumers passed. glibc execution used the SDK runtime;
this does not prove compatibility with an older distribution's glibc.

A separate native loopback mTLS probe uses disposable SoftHSM keys and an
unmodified libp11 0.4.21 provider built against public OpenSSL 4.0.3. RSA with
negotiated TLS and EC P-256 with TLS 1.2 accepted a valid PIN and rejected an
invalid PIN. EC P-256 with TLS 1.3 failed in the OpenSSL command-line control
client as well as the rstream diagnostic; libp11 0.4.20 showed the same control
failure. Do not claim general EC/TLS 1.3 PKCS#11 support from the successful
RSA/TLS 1.2 cases. The alternative OpenSSL Projects provider 1.3.0 passes the standalone EC/TLS 1.3
control when the SoftHSM fixture is isolated from global OpenSSL configuration.
It exposed a rstream initialization error: module/PIN parameters were added
after provider loading. The fix supplies them with `OSSL_PROVIDER_load_ex`
on OpenSSL 3.2+, before provider initialization. No provider or public Conan
dependency source was patched. The first native rebuild passed 52 tests and its external consumer. The local
fixture passed EC/TLS 1.2 and 1.3 plus RSA/TLS 1.3 with this provider, and retained
libp11 EC/TLS 1.2 and RSA/TLS 1.3 support; every case also rejected an invalid PIN.
The complete cross/native matrix and all matching archives/runtime checks
subsequently passed on `61b9425`; CI remains a separate acceptance gate.

A disposable CE engine pinned to image digest
`sha256:40b6c8e56b3be11ed6e15e02699378e975738d58991d86253e2ce65c55bc498d`
passed local network qualification with the final native binary and all four
extracted distribution packages. Each run passed passthrough creation plus
seven runtime cases: two invalid-option checks, terminated TLS, TLS upstream,
TLS passthrough, HTTP/1.1 and published TCP. ARM64 used QEMU; glibc used the
SDK runtime. Evidence and the exact adapted script are in
`../.yocto-pilot/local-engine-e2e-provider-init/<cell>/`. The final archive and
runtime queues both completed successfully; all 18 runtime checks passed
(eight terminfo probes, five CE cells and five PKCS#11 provider cases).

The runtime script uses CE-compatible settings: it omits custom edge ALPN
policy and the hosted cross-region routing flag. The original hosted script
reproduced `feature not available` for those options against CE; these are not
reported as successful hosted policy tests. The engine listened only on
loopback, used a temporary certificate and short-lived test JWT, and was
removed after each run. No hosted resources or credentials were changed.

Hosted engine/mTLS/PKCS#11 suites remain pending selection of a test environment.
The configured context is a hosted production project; available projects are
all Pro, while the credential suites also exercise Basic-plan rejections.
Native CI and sanitizer results must be checked separately before completion.

## Local PKCS#11 regression fixture

`test/e2e/rstream-pkcs11-local.py` creates disposable SoftHSM keys, checks an
OpenSSL control connection, then tests the selected rstream binary with a valid
and an invalid PIN. It uses loopback only and removes the token directory on
exit. Set `LD_LIBRARY_PATH` if the extracted test tools need additional shared
libraries. Use unmodified public provider builds matching the OpenSSL runtime:

```bash
python3 test/e2e/rstream-pkcs11-local.py \
  --binary /path/to/rstream-ncat --openssl /path/to/openssl \
  --tools-prefix /path/to/softhsm-and-opensc/usr \
  --module /path/to/libsofthsm2.so \
  --provider-dir /path/to/ossl-modules --provider pkcs11 \
  --tls-version 1.3 --output /path/to/evidence
```

Use `--rsa` for RSA instead of EC P-256, and `--provider pkcs11prov` for libp11.
The fixture deliberately avoids global provider configuration: SoftHSM's own
OpenSSL backend must not recursively activate the provider being tested.
This is a local integration check, not physical HSM or hosted-policy certification.

## Older-kernel probe

An isolated KVM guest running Ubuntu kernel `5.4.0-216-generic` (package
`5.4.0-216.236`, downloaded through Ubuntu's signed APT metadata) passed the
final x86_64/musl C++20 threads/filesystem smoke program, a TCP payload exchange,
and an mTLS 1.3 HTTP exchange with the final rstream-ncat client and a reference
OpenSSL server. The guest had no external network and changed neither the host
kernel nor the SDK build settings. Exact executable/image hashes and the guest
console log are in `../.yocto-pilot/kernel-probe/`.

This is evidence for those selected x86_64/musl programs on that kernel, not a
universal minimum-kernel guarantee. ARM64 and glibc were not tested on 5.4; the
configured upstream 5.15 baseline remains unchanged.

The initial `ncat -c` TLS test-server pattern transmitted its payload then
returned `stream truncated` to the client. The same result reproduces with the
prior source and both OpenSSL 3 and 4 on host kernel 7.0. It is recorded in
`kernel-probe/ncat-server-control.json`; it is not counted as a passed TLS
shutdown test. The successful compatibility probe used OpenSSL's server to
provide proper TLS shutdown.

## Subsequent CI evidence

The dependency-refresh Build run
[37130063923](https://github.com/rstreamlabs/rstream-cpp/actions/runs/37130063923)
completed successfully on `bc38d15`, including both Windows linkage variants.
Its static Windows log resolves NASM 3.01, Strawberry Perl 5.40.2.1 and jom 1.1.4;
the compatibility builds also use upstream-pinned Perl 5.32.1.1 and public Boost
1.83 alongside the default Boost 1.91/OpenSSL 4 graph. Exact extracted versions
are recorded in `../.yocto-pilot/windows-tools-refresh-versions.json`.

The earlier Reliability run
[37128288587](https://github.com/rstreamlabs/rstream-cpp/actions/runs/37128288587)
also completed successfully, including Windows. These runs qualify their own
revisions. The final provider-initialization source `61b9425` passed the entire
[Reliability run 37133042984](https://github.com/rstreamlabs/rstream-cpp/actions/runs/37133042984),
including Linux, macOS, both Windows linkage variants, stress, static analysis
and sanitizer checks. Its separate
[Build run 37133028526](https://github.com/rstreamlabs/rstream-cpp/actions/runs/37133028526)
also passed all jobs, including both Windows linkage variants. Hosted-policy
tests remain a separate gate.
