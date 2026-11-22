# Conan packages and toolchains

## Supported environments

The supported platform contract includes:

- Linux with GCC and Clang;
- macOS on Intel and Apple Silicon;
- Windows with MSVC for native development and CI;
- MinGW cross-built Windows distribution packages;
- glibc Linux packages built against the configured Yocto SDK baseline;
- musl Linux packages for standalone, broadly portable binaries.

The required native Windows package matrix uses the Visual Studio 2022 runner
baseline (`windows-2022`) for all library and plugin combinations, with strict
warnings and warnings as errors. A runner upgrade is a toolchain change to
qualify explicitly. The external Conan consumer can select Boost 1.83 within
the supported range and rebuild the SDK, even when the initial package used
Boost 1.89. Optimized MSVC builds can report C4702 in Boost 1.83's
`const_buffer` conversion. Affected application and test translation units
suppress only that diagnostic while defining the external Asio buffer header,
then restore the warning state before any SDK definitions. No runtime code or
optimization settings change. Windows 11 ConPTY runtime checks remain part of
the cross-language WebTTY matrix.

Library linkage and plugin loading are independent choices:

| SDK libraries | Plugins | Required |
| ------------- | ------- | -------- |
| Static        | Static  | Yes      |
| Static        | Dynamic | Yes      |
| Shared        | Static  | Yes      |
| Shared        | Dynamic | Yes      |

`BUILD_SHARED_LIBS` selects SDK library linkage.
`ENABLE_STATIC_PLUGINS` selects build-time plugin registration or runtime
module loading. A build must not silently replace one requested topology with
another. Every topology must work from the build tree, install tree, Conan
package, and an external consumer.

Static plugins must remain reachable with dead-code elimination enabled.
Deploy dynamic plugins with the SDK libraries they were built against. The
loader uses the plugin's initialization entry point and reports its version.
It does not check ABI compatibility. Preserve the install layout, symbol
visibility and failure isolation. Loading one invalid module must not prevent
valid modules from loading. A validated dynamic module remains loaded for the
process lifetime, keyed by its canonical path. Destroying a factory must not
unload and later reload modules whose process-wide registries cannot be safely
registered twice.

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
criteria are maintained in [local recipe inventory](recipes/README.md).

## Dependency constraints

Dependency ranges describe accepted inputs, not the libraries linked into a
particular binary. Resolve a fresh Conan graph with `conan graph info . --update
--format=json`, retain the graph with build evidence, then build and run tests.
The `rstream-test-ssl-provider` test compares the selected header and runtime
version and writes `ssl-provider.json` in the test directory. Native Conan
packages retain that result under `res/` when tests ran. Cross-built packages
still need execution on their target before claiming runtime qualification.

The [public Conan recipe](../conanfile.py) owns package ranges. CMake checks
native provider versions in [dependencies](../cmake/dependencies.cmake) and the
[OpenSSL version policy](../cmake/ssl-version.cmake). Use those inputs when
resolving a build so the guide stays aligned with the code.

| Family   | Constraint                                                                                                                             |
| -------- | -------------------------------------------------------------------------------------------------------------------------------------- |
| OpenSSL  | Satisfy the supported release range and security patch floor in both Conan and CMake. Cached packages must satisfy the selected graph. |
| LibreSSL | Satisfy the provider floor. A native build can use the official CMake package when a compatible Conan package is unavailable.          |
| protobuf | Use the same Conan reference for compiler and runtime.                                                                                 |
| Boost    | Keep the public recipe range, Asio/Cobalt metadata and target-specific recipe overrides compatible.                                    |

The LibreSSL build omits OpenSSL ENGINE support, PKCS#11 provider storage and
RPK negotiation;
unsupported configuration fails explicitly. Header search order must match the
selected provider, including when Homebrew exposes a different OpenSSL major in
its general include directory. TLS certificate identity checks remain active
for both DNS names and IP SANs. IP literals are not sent in the DNS-only SNI
extension. The TLS runtime test covers valid and invalid DNS/IP identities,
configuration rejection (including each unsupported RPK direction), handshake/data
transfer and bounded shutdown. Both explicit identities and URL-derived IPv4/IPv6
literals are covered.

Retain source revisions, provider reports and test results for each native
build matrix; a graph from a different platform is not runtime evidence.

## Conan package matrix

Validate all four library and plugin topologies on Linux, macOS, and Windows:

```bash
conan create . --build="missing" --build="rstream/*" -pr:b default -pr:h default \
  -o 'rstream/*:shared=False' \
  -o 'rstream/*:static_plugins=True'

conan create . --build="missing" --build="rstream/*" -pr:b default -pr:h default \
  -o 'rstream/*:shared=False' \
  -o 'rstream/*:static_plugins=False'

conan create . --build="missing" --build="rstream/*" -pr:b default -pr:h default \
  -o 'rstream/*:shared=True' \
  -o 'rstream/*:static_plugins=True'

conan create . --build="missing" --build="rstream/*" -pr:b default -pr:h default \
  -o 'rstream/*:shared=True' \
  -o 'rstream/*:static_plugins=False'
```

Each package must pass its `test_package` as an external consumer. CI also
generates and verifies the public dependency graph. Run its focused regression
tests locally with:

```bash
python3 test/test_conan_public_dependencies.py
```

## Distribution packaging

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
