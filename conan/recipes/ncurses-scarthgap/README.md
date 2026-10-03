# Scarthgap ARM64 packaging candidate

This unpublished candidate is restricted to compiled distribution packages.
Library consumers retain unmodified public Conan Center recipes.

The public ncurses 6.5 recipe rejects cross compilation to or from ARM before
building, including the pinned Yocto 5.0.10 ARM64 profile. The same restriction
was confirmed in the current Conan Center Index source.

This candidate derives from the recorded unmodified public recipe. It permits
only Linux x86_64 to Linux ARM64 with the exact Yocto 5.0.10 musl or glibc SDK.
It retains the public recipe's native build compiler detection. As in the
qualified Wrynose packaging variant, install-time stripping is disabled for
cross builds so the host strip is not applied to target binaries. No ncurses
source is patched. Final package stripping uses target tools.

CMake export inputs are limited to the recipe cmake directory so generated
consumer files cannot change its revision. upstream.json records public file
hashes and upstream.diff is the complete delta.

The ARM64/musl candidate passed the included C++ consumer under QEMU with
Yocto 5.0.10 / GCC 13.3. It checks library calls and the installed terminal
database. The glibc consumer and full Scarthgap rstream package remain pending.
No Scarthgap result is inferred from the earlier Wrynose qualification.
