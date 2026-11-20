# Build for Linux ARM64 with GCC

Use your target's compiler and libc when an application already ships with
a Linux toolchain. In this example the build machine runs x86-64 Ubuntu
24.04 and the target runs ARM64 Linux with glibc. The SDK and its
dependencies come from this checkout and public Conan Center.

Start from a clean checkout. Install the [source build prerequisites](../001-building-from-source.md#prerequisites)
and Ubuntu's native and cross GCC 13 packages. Ubuntu's cross C++ package
installs the ARM64 libc development files and target binutils as
dependencies.

```sh
sudo apt-get install gcc-13 g++-13 gcc-13-aarch64-linux-gnu g++-13-aarch64-linux-gnu
```

## Select the two compilers

Use the native profile for programs that run during the build, including
the Protobuf generator. Use the ARM64 profile for the SDK and target
libraries.

```sh
CC=/usr/bin/gcc-13 CXX=/usr/bin/g++-13 conan profile detect --name rstream-gcc13 --exist-ok
cat > linux-arm64.profile <<'EOF'
[settings]
os=Linux
arch=armv8
compiler=gcc
compiler.version=13
compiler.libcxx=libstdc++11
compiler.cppstd=20
build_type=Release

[conf]
tools.build:compiler_executables={'c': '/usr/bin/aarch64-linux-gnu-gcc-13', 'cpp': '/usr/bin/aarch64-linux-gnu-g++-13'}
tools.cmake.cmaketoolchain:system_name=Linux
tools.cmake.cmaketoolchain:system_processor=aarch64
tools.gnu:host_triplet=aarch64-linux-gnu

[buildenv]
CC=/usr/bin/aarch64-linux-gnu-gcc-13
CXX=/usr/bin/aarch64-linux-gnu-g++-13
AR=/usr/bin/aarch64-linux-gnu-ar
RANLIB=/usr/bin/aarch64-linux-gnu-ranlib
STRIP=/usr/bin/aarch64-linux-gnu-strip
EOF
conan profile show -pr:b rstream-gcc13 -pr:h ./linux-arm64.profile
```

Check that the build profile reports `x86_64` and GCC 13 and that the
host reports `armv8`. If a saved build profile names a different compiler,
detect it under a different profile name before continuing.

## Build and check the target

```sh
conan install . --output-folder=build/arm64 --build=missing \
  -pr:b rstream-gcc13 -pr:h ./linux-arm64.profile -s:b compiler.cppstd=20 \
  -o '&:enable_testing=False' -o '&:with_ncurses=False' \
  -o '&:with_maxminddb=False' -o 'boost/*:without_cobalt=True'
cmake --preset conan-release
cmake --build --preset conan-release --target rstream-tunnel
readelf -hW build/arm64/build/Release/bin/tunnel/bin/rstream-tunnel
readelf -lW build/arm64/build/Release/bin/tunnel/bin/rstream-tunnel
readelf -dW build/arm64/build/Release/bin/tunnel/bin/rstream-tunnel
readelf --version-info build/arm64/build/Release/bin/tunnel/bin/rstream-tunnel
```

The ELF header should report `AArch64`. This configuration links the SDK
libraries and plugins statically. The system loader and the C and C++
runtimes can still be shared. Check the ELF interpreter, shared-library
requirements, and required `GLIBC_*` and `GLIBCXX_*` versions. Test on
the oldest ARM64 system you support. On that system, run `rstream-tunnel --version` and
follow the [service forwarding walkthrough](../tools/000-forwarding.md).
For a custom sysroot, adjust the compiler and flags in the host
profile to use the same sysroot as your application and rebuild its
dependencies with that profile.
