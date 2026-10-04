# Scarthgap ARM packaging candidate

This unpublished candidate is restricted to compiled distribution packages.
Library consumers retain unmodified public Conan Center recipes.

The public ncurses 6.5 recipe rejects cross compilation to or from ARM before
building, including the pinned Yocto 5.0.10 ARM64 profile. The same restriction
was confirmed in the current Conan Center Index source.

This candidate derives from the recorded unmodified public recipe. It permits
Linux x86_64 or ARM64 SDK hosts targeting Linux ARM64, and Linux x86_64
SDK hosts targeting Linux ARMv7 hard-float, with the exact Yocto 5.0.10
musl or glibc SDK. ARMv7 soft-float, mismatched ABI identities, other SDK
versions and ARM64 hosts targeting ARMv7 are rejected.
It retains the public recipe's native build compiler detection. As in the
qualified Wrynose packaging variant, install-time stripping is disabled for
cross builds so the host strip is not applied to target binaries. No ncurses
source is patched. Final package stripping uses target tools.

CMake export inputs are limited to the recipe cmake directory so generated
consumer files cannot change its revision. upstream.json records public file
hashes and upstream.diff is the complete delta.

The x86_64-host ARM64/musl and ARM64/glibc candidates passed the included C++
consumer under QEMU with Yocto 5.0.10 / GCC 13.3, along with the full rstream
package, external consumer, distribution archives and local engine tests.
The focused ncurses consumer checks library calls and the installed terminal
database. The ARM64-host/ARM64-target musl build and focused consumer also
passed in an emulated Linux ARM64 userspace. The complete ARM64-host rstream
package chain and the ARM64-host glibc consumer remain pending; no native
ARM hardware performance result is claimed.

The x86_64-host ARMv7 hard-float/musl SDK and focused QEMU C++/terminfo
consumer passed, as did the graph boundary controls. Full rstream distribution
qualification remains pending.
