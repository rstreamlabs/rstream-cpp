# Validate SDK changes

## Build and package validation

Run the checks below from a clean checkout with compatible dependencies and a
CMake toolchain already provisioned. The [source build](../docs/001-building-from-source.md)
provides a first Conan-backed native build. Its generated `conan-debug` preset
and the repository `quality` preset are separate build configurations: supply
the dependency toolchain explicitly when configuring `quality` (for example
`cmake --preset quality -DCMAKE_TOOLCHAIN_FILE=/absolute/path/to/conan_toolchain.cmake`).
Use a toolchain and dependencies matching the requested linkage/plugin profile;
sanitizers require their instrumented graph described below.
Warm-cache reruns may be used for
iteration, but final acceptance must validate the exact source revision.

### Native quality

```bash
cmake --preset quality
cmake --build --preset quality
ctest --preset quality
```

Project warnings are errors in quality and CI builds. Warning suppression is
limited to generated or third-party code and must not hide project warnings.

### Sanitizers and concurrency

```bash
# Linux: ASan, UBSan and LSan with static libraries/plugins.
python3 .github/scripts/test-memory-sanitizers.py --output out/memory-sanitizers --jobs 3

# macOS: ASan and UBSan with shared SDK libraries/dynamic plugins.
python3 .github/scripts/test-memory-sanitizers.py --output out/memory-sanitizers --jobs 3 --shared --dynamic-plugins

cmake --preset tsan
cmake --build --preset tsan
ctest --preset tsan

ctest --test-dir out/build/quality --repeat until-fail:20 \
  --output-on-failure \
  -R 'core-(executor-binder|plugin-version)|io-common-(payloader-limits|queue|stream-tcp)|io-rstrm-(control-channel|handshake)|nperf-runtime|tunnel-proxy|webtty-.*runtime'
```

Memory checks use Conan 2.34.0, Ninja and GNU coreutils (`timeout` on
Linux, `gtimeout` on macOS). The runner instruments the dependency graph and
includes instrumentation flags in Conan package identities, so cached release
libraries cannot be substituted. Protobuf's generated code and runtime must
use compatible instrumentation; mixing an ASan application with a prebuilt
Homebrew runtime can fail inside parsing or descriptor access. Use the `asan`
preset only with an already compatible dependency toolchain. Leak detection
is required on Linux; macOS does not provide that runtime. Both platforms keep
ASan and UBSan failures fatal and preserve JUnit and dependency provenance.

Run static analysis and the coverage preset for changes that affect public
operations, ownership, state machines, or shared runtime code. Coverage is a
diagnostic: new assertions must exercise failure and concurrency behavior, not
only increase a percentage.

```bash
cmake --preset coverage
cmake --build --preset coverage
ctest --preset coverage

run-clang-tidy \
  -p out/build/quality \
  -warnings-as-errors="*" \
  '^.*/rstream-cpp/(lib|bin|plugin)/.*\.cpp$'
```

Use the LLVM installation selected by the `Reliability` workflow when the
system `run-clang-tidy` does not match the compiler used to produce
`compile_commands.json`.

### Engine runtime

After building `rstream-tunnel`, put it on the path and select your local
Engine context:

```bash
export RSTREAM_CONTEXT='my-local-context'
test/e2e/rstream-tunnel-passthrough.sh
test/e2e/rstream-tunnel-runtime.sh
```

Run the mTLS and PKCS#11 suites when authentication, TLS, configuration, or
credential handling changes. The final test report must identify every skipped
suite and why it was not applicable.

## Required Windows checks

The native Windows package matrix also enables `RSTREAM_TEST_WINDOWS_PIPE_ASAN`.
This MSVC Release/RelWithDebInfo target compiles the pipe adapter and its tests
with AddressSanitizer, including their Boost headers, without linking an
uninstrumented SDK copy. A stateful allocator and late cancellation exercise
control-block lifetime after the erased completion handler has been destroyed.
The option is off for ordinary consumers and requires the MSVC ASan component
when enabled; both consumer-selected Boost versions are exercised in CI.
The workflow passes a typed CMake cache boolean so legacy `option()` policies
cannot reset the request. Conan builds the required target before the full SDK
and checks its fresh JUnit result after the suite runs. A missing, skipped or
failed required test cannot qualify the package. The workflow configuration
and report checks have a regression test in `test/test_conan_windows_asan.py`.
All native package jobs force the SDK build while reusing cached dependencies.
`--build=missing` alone can reuse the SDK binary and bypass its internal tests
on a repeated CI run. The regression suite checks the actual Conan cache
behavior and requires execution even when that binary is already cached.

See the [Conan package matrix](../conan/README.md#conan-package-matrix) for external package consumption.
