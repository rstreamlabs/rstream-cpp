# SDK Engineering Contract

This document defines the engineering contract for `rstream-cpp`. It is the
acceptance baseline for changes to the SDK, its dependencies, build system,
plugins, packages, and asynchronous runtime.

The SDK targets constrained native systems. Portability, predictable resource
usage, and Boost.Asio compliance are product requirements, not optional build
qualities.

## Supported environments

The supported platform contract includes:

- Linux with GCC and Clang;
- macOS on Intel and Apple Silicon;
- Windows with MSVC for native development and CI;
- MinGW cross-built Windows distribution packages;
- glibc Linux packages built against the configured Yocto SDK baseline;
- musl Linux packages for standalone, broadly portable binaries.

Library linkage and plugin loading are independent choices:

| SDK libraries | Plugins | Required |
| --- | --- | --- |
| Static | Static | Yes |
| Static | Dynamic | Yes |
| Shared | Static | Yes |
| Shared | Dynamic | Yes |

`BUILD_SHARED_LIBS` selects SDK library linkage.
`ENABLE_STATIC_PLUGINS` selects build-time plugin registration or runtime
module loading. A build must not silently replace one requested topology with
another. Every topology must work from the build tree, install tree, Conan
package, and an external consumer.

Static plugins must remain reachable with dead-code elimination enabled.
Dynamic plugins must preserve their install layout, version checks, symbol
visibility, and failure isolation. Loading one invalid module must not prevent
valid modules from loading. A validated dynamic module remains loaded for the
process lifetime, keyed by its canonical path. Destroying a factory must not
unload and later reload modules whose process-wide registries cannot be safely
registered twice.

## Constrained-system contract

Runtime behavior must be explicit and bounded:

- queues, buffers, retries, pending operations, and worker counts have limits;
- the SDK does not create hidden worker threads;
- blocking work does not run on Asio event-loop threads;
- hot paths do not perform avoidable filesystem, process, or network I/O;
- object ownership and shutdown order remain deterministic;
- cancellation and destruction release work and resources promptly;
- malformed or hostile input cannot cause unbounded allocation or retry loops;
- logs and errors provide actionable context without exposing credentials.

New background work, caches, locks, queues, or allocations require a stated
reason and tests for exhaustion, cancellation, and shutdown. Performance work
must be measured; it must not trade correctness for an assumed optimization.

## Boost.Asio contract

Every asynchronous public operation behaves as a regular Asio operation:

- use `async_initiate` or an equivalent composed-operation mechanism;
- preserve the associated executor, allocator, cancellation slot, and
  move-only completion handler;
- complete exactly once on success, failure, cancellation, and destruction;
- document and test immediate versus deferred completion when observable;
- retain outstanding work only for the lifetime of the operation;
- serialize mutable asynchronous state through its executor or strand;
- use mutexes only for state also accessed synchronously or across executors;
- never invoke an external callback while holding a lock;
- never depend on sleeps for operation ordering in tests.

Tests must cover overlapping operations, cancellation before and during I/O,
partial I/O, peer failure, reconnect, close, destruction, and handler
reentrancy where applicable. ThreadSanitizer and repeated lifecycle tests are
acceptance gates for changes to shared state.

## rstream runtime contract

The SDK must preserve the rstream contract from configuration input to runtime
behavior:

1. Parse SDK options, URI parameters, environment variables, the selected
   context, the configuration file, and defaults.
2. Apply the documented precedence without silently ignoring a supported
   value.
3. Normalize values and reject invalid combinations, unsupported features,
   and conflicting authentication modes.
4. Produce an immutable effective configuration with secrets redacted from
   errors and logs.
5. Map the effective configuration to internal state and protobuf fields
   without changing its meaning.
6. Preserve engine error semantics, capability checks, cancellation, retry,
   reconnect, and shutdown behavior.

Configuration changes require positive, negative, precedence, wire-mapping,
and runtime tests. Shared configuration behavior must remain aligned with the
Go reference SDK. Unsupported rstream features must fail explicitly rather
than fall back to a different behavior.

## Conan dependency policy

The public `rstream` Conan recipe must be consumable using public Conan Center
recipes only:

- `conan create .` must work without repository-local dependency recipes;
- the public dependency graph must not contain a Conan user or channel;
- dependency recipe metadata must identify Conan Center as its source;
- consumers must never need a patched rstream dependency remote;
- a local recipe must not be added merely to repair a developer workstation.

`conan/check_public_dependencies.py` enforces this contract in CI. Private
references are rejected by `conanfile.py` unless the build explicitly provides
the distribution metadata `build_os`, `build_arch`, and `build_channel`.

Distribution packaging may override a dependency only when an upstream Conan
Center recipe cannot support a required target, such as the configured Yocto
SDK. An override must:

- be used only by `build-conan-cross.sh`;
- be controlled through `USE_PATCHED_CONAN_DEPS`, with patched dependencies
  confined to the distribution packaging flow;
- contain the smallest possible delta from the matching Conan Center recipe;
- document the affected platform and the upstream limitation;
- avoid changing behavior for unaffected targets;
- include a focused package test for the required target;
- never become a transitive requirement of the public `rstream` package.

Repository-local recipes currently exist only for packaging constraints. Do
not add a custom protobuf recipe: the public Conan Center protobuf package is
part of the SDK package contract.

Before each release and every dependency update, compare each local recipe
with its matching Conan Center revision. Remove the override when upstream
supports the required target. A recipe retained after review must still have a
documented, reproducible reason. The current recipe inventory and removal
criteria are maintained in `conan/recipes/README.md`.

## Build and package validation

Run the checks below from a clean checkout. Warm-cache reruns may be used for
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
cmake --preset asan
cmake --build --preset asan
ctest --preset asan

cmake --preset tsan
cmake --build --preset tsan
ctest --preset tsan

ctest --test-dir out/build/quality --repeat until-fail:20 \
  --output-on-failure \
  -R 'core-(executor-binder|plugin-version)|io-common-(payloader-limits|queue|stream-tcp)|io-rstrm-(control-channel|handshake)|nperf-runtime|tunnel-proxy|webtty-.*runtime'
```

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

### Conan package matrix

Validate all four library and plugin topologies on Linux, macOS, and Windows:

```bash
conan create . --build=missing -pr:b default -pr:h default \
  -o 'rstream/*:shared=False' \
  -o 'rstream/*:static_plugins=True'

conan create . --build=missing -pr:b default -pr:h default \
  -o 'rstream/*:shared=False' \
  -o 'rstream/*:static_plugins=False'

conan create . --build=missing -pr:b default -pr:h default \
  -o 'rstream/*:shared=True' \
  -o 'rstream/*:static_plugins=True'

conan create . --build=missing -pr:b default -pr:h default \
  -o 'rstream/*:shared=True' \
  -o 'rstream/*:static_plugins=False'
```

Each package must pass its `test_package` as an external consumer. CI also
generates and verifies the public dependency graph. Run its focused regression
tests locally with:

```bash
python3 test/test_conan_public_dependencies.py
```

### Distribution packaging

The release-equivalent cross-platform command is:

```bash
EXPORT_PACKAGE_NAME="rstream-utils" \
LINUX_TCLIBCS="musl" \
LINUX_BUILD_SHARED="off" \
MACOS_BUILD_SHARED="on" \
WINDOWS_BUILD_SHARED="off" \
./build-conan-cross.sh
```

When changing packaging or a local recipe, also run focused profiles for the
affected architecture, libc, linkage, and plugin mode. Validate both archive
contents and execution of representative binaries. A successful compilation
without package consumption or runtime execution is insufficient.

### Engine runtime

After building `rstream-tunnel`, run the engine E2E suites against a local
engine:

```bash
export RSTREAM_CONTEXT=<local-context>
test/e2e/rstream-tunnel-passthrough.sh
test/e2e/rstream-tunnel-runtime.sh
```

Run the mTLS and PKCS#11 suites when authentication, TLS, configuration, or
credential handling changes. The final test report must identify every skipped
suite and why it was not applicable.

## Dependency update checklist

A dependency update is complete only when:

1. The requested version exists in Conan Center for normal SDK consumers.
2. The public graph check passes without private users or channels.
3. Any local override is compared with the new Conan Center recipe and removed
   unless its original target limitation remains.
4. Retained recipe changes remain minimal and their package tests pass.
5. The four linkage/plugin topologies pass on Linux, macOS, and Windows.
6. The Yocto glibc and standalone musl package paths still work.
7. Native quality, sanitizers, static analysis, and relevant E2E tests pass.
8. Build duration, package size, warnings, and runtime resource behavior do not
   regress without an explicit, documented tradeoff.

Do not solve a dependency update by disabling a topology, weakening warnings,
removing a test, replacing a public dependency with a private one, or silently
changing runtime behavior.
