# Cross-compile the C++ SDK and tools

Run the build with two Conan profiles. The **build profile** describes
the machine that runs tools such as `protoc`. The **host profile**
describes the target compiler, ABI and libraries linked into the output.
For example, a Linux x86-64 to ARM64 build needs x86-64 `protoc` and
ARM64 Protobuf and TLS libraries.

| Goal | Start with |
| --- | --- |
| Build a tool or SDK library for a target with your own compiler | Use separate Conan profiles and a fresh CMake build directory. Follow the [Intel macOS build](#apple-silicon-to-intel-macos) or the [Linux ARM64 build with GCC](cross/000-linux-gcc.md). |
| Build the distributed Linux utilities for a device | Use the [ARM64 distribution build](#build-the-linux-arm64-distribution) and its target SDK. |
| Ship an application already linked against the SDK | Rebuild the application with the same host profile as its SDK and preserve the [plugin layout](009-plugins-and-packaging.md#choose-how-the-application-ships-them). |

## Apple Silicon to Intel macOS

Use an Apple Silicon Mac with Apple's command-line toolchain, Conan, CMake
and Ninja. Start in a fresh checkout to keep the generated Release preset
and dependencies separate from native builds.

Detect the build machine profile. Create a portable host profile that uses
the same Apple Clang and selects Intel x86-64.

```sh
conan profile detect --name rstream-local --exist-ok
cat > intel-macos.profile <<'EOF'
include(rstream-local)

[settings]
os=Macos
arch=x86_64
compiler.cppstd=20
build_type=Release
EOF
```

Check the two contexts before compiling.

```sh
conan profile show -pr:b rstream-local -pr:h ./intel-macos.profile
```

The build context should show `armv8` and the host context `x86_64`.
Resolve target dependencies with the host profile. This example builds the
tunnel tool without optional terminal or geolocation libraries.

```sh
conan install . --output-folder=build/intel --build=missing \
  -pr:b rstream-local -pr:h ./intel-macos.profile -s:b compiler.cppstd=20 \
  -o '&:enable_testing=False' -o '&:with_ncurses=False' \
  -o '&:with_maxminddb=False' -o 'boost/*:without_cobalt=True'
cmake --preset conan-release
cmake --build --preset conan-release --target rstream-tunnel
file build/intel/build/Release/bin/tunnel/bin/rstream-tunnel
```

`file` should report a Mach-O `x86_64` executable. Run it with
`--version` on an Intel Mac, or on Apple Silicon with Rosetta installed:

```sh
arch -x86_64 build/intel/build/Release/bin/tunnel/bin/rstream-tunnel --version
```

The Conan toolchain uses the selected Apple SDK. Set `os.version` to the minimum macOS version in the host profile when shipping to older systems. Check
the resulting load commands with `otool -l` and test on the oldest
system you support.

## Build an SDK package for your application

Use the same two profiles when packaging the SDK and building its
consumer. On the Apple Silicon setup above, create the Intel SDK in
the local Conan cache.

```sh
conan create . --build=missing \
  -pr:b rstream-local -pr:h ./intel-macos.profile -s:b compiler.cppstd=20 \
  -o '&:build_bins=False' -o '&:enable_testing=False' -o '&:with_ncurses=False' \
  -o '&:with_maxminddb=False' -o 'boost/*:without_cobalt=True'
```

Conan builds the repository's [external consumer](../test_package) against
the packaged SDK. During a cross-build Conan skips executing it. On Intel
macOS or with Rosetta, run `bin/test_package` from the test package build
directory printed by Conan. It should exit successfully after loading the
TCP provider.

In your own [CMake application](000-getting-started.md#link-an-existing-cmake-target),
require the version reported by `conan create`. Run `conan install` with the
same build and host profiles and use the corresponding package options.
In a consumer recipe, set options such as `rstream/*:build_bins=False`
on the SDK dependency. Build with the generated Release preset, then test
on the target. The Linux GCC host profile follows the same packaging model.

## Build the Linux ARM64 distribution

To reproduce the native tools layout used for ARM64 Linux, start on
x86-64 Linux. Use Bash 4 or later, Python with PyYAML, Conan 2.34, CMake,
Ninja, Git, a native C++ compiler and the standard archive tools. See
the [release workflow](../.github/workflows/release-packages.yml) for the
build host and its package list. The distribution uses a target toolchain
from the rstream Conan remote. For an application built with its own compiler and
sysroot, use the [GCC and public Conan Center example](cross/000-linux-gcc.md).

From the checkout root, use an isolated Conan home for the distribution profiles.

```sh
export CONAN_HOME="$PWD/build/conan-linux-arm64"
conan profile detect
conan config install conan/config
conan remote add rstream https://nexus.rstream.io/repository/conan
CHANNEL=stable OSS=linux USE_DOCKER=off LINUX_ARCHS=arm64 \
  LINUX_TCLIBCS=musl LINUX_BUILD_SHARED=off ./build-conan-cross.sh
```

This selects one ARM64 musl build and uses static plugins. The default
script action builds and exports. The `build` subcommand only prepares
Conan packages. It does not export the finished archive.

The archive is
`out/release/<sdk-version>/linux/arm64/musl/static/packages/rstream-utils.tar.gz`.
Its version comes from the checkout's `version.txt` or the selected release
metadata. Inspect the archive and the extracted executable:

```sh
sdk_version=$(conan inspect . --format=json | python3 -c 'import json, sys; print(json.load(sys.stdin)["version"])')
archive="out/release/$sdk_version/linux/arm64/musl/static/packages/rstream-utils.tar.gz"
tar -tzf "$archive" | sed -n '1,16p'
mkdir -p build/check-arm64
tar -xzf "$archive" -C build/check-arm64
file build/check-arm64/bin/rstream-tunnel
readelf -hW build/check-arm64/bin/rstream-tunnel
```

The ELF header should identify `AArch64`. Run the extracted
`bin/rstream-tunnel --version` on an ARM64 target. Then use the
[forwarding walkthrough](tools/000-forwarding.md) to verify a TLS
Engine connection, a response from the local service and clean shutdown.

If your target requires glibc and shared SDK libraries, rerun the build with
`LINUX_TCLIBCS=glibc LINUX_BUILD_SHARED=on`. The output is under
`out/release/<sdk-version>/linux/arm64/glibc/shared/packages/`.
Keep the SDK libraries, dependencies and plugins together when testing that build.

## Build and run on the target

Use the same host profile for the application's SDK and libraries. Copy
its executable and [runtime files](009-plugins-and-packaging.md#preserve-the-install-layout)
to the target. Verify the application's real TLS connection, input and
output, and shutdown. If the device has tighter resource limits than the
build host, repeat the exchange with the concurrency and deadlines you
expect in service.

## Diagnose a cross-build

| Symptom | Check |
| --- | --- |
| Linker reports an incompatible target | Resolve the SDK and every linked library with the same host profile. Inspect the imported library architecture. |
| `protoc` fails to start during compilation | Check that the generator came from the build graph and matches the Protobuf library protocol and version expected by the host graph. |
| Target reports a missing library or loader | Inspect the ELF interpreter with `readelf -lW` and dependencies with `readelf -dW`. Check the target's libc and deploy the required libraries and plugins together. |
| The executable starts but the tunnel fails | Exercise the actual device's DNS, clock, trusted CA, credentials and project policy. Reuse the same input and expected response as on the build machine. |
