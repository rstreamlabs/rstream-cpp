# Wrynose ARM64 packaging exception

This candidate is for compiled distribution packages only. Library consumers
must continue to use unmodified public Conan Center recipes.

The base is `ncurses/6.5#d35d5bc6bbe86cf25c10957df6302f8f` from
`https://center2.conan.io`. `upstream.json` records the downloaded file hashes;
`upstream.diff` records the complete recipe delta. No ncurses source is patched.

The public recipe rejects every cross-build to or from ARM before compilation.
The exception permits Linux x86_64 to Linux ARM64 using Yocto 6.0.x. From a
Linux ARM64 SDK host, it permits only the exact Yocto 6.0.3 musl/glibc SDK
identities for ARM64, x86_64, x86_64_v2 and x86_64_v3 targets. Other ARM build-host
combinations remain rejected. Upstream's native build compiler detection is
retained and must be verified in the build log.

Recipe acceptance does not qualify a target. Each host/target/libc combination
requires an actual SDK, the focused ncurses consumer, and the full rstream
package tests and external consumer. ISA-level targets require an explicitly
verified runner: the focused v2 consumer uses `qemu-x86_64 -cpu Nehalem`, and
v3 uses `qemu-x86_64 -cpu Haswell,-hle,-rtm`. HLE and RTM are optional Haswell
features outside the x86-64-v3 baseline. The glibc consumer invokes the SDK's
loader with `--inhibit-cache` and explicit library paths.

The first ARM64 build compiled successfully, then installation failed because
`install -s` invoked the workstation's x86_64 `strip` on an ARM64 `tic`. The
recipe disables install-time stripping for cross builds; ncurses source and
compiler flags remain unchanged. Final distribution stripping uses target
tools.

The local export also restricts CMake inputs to the recipe's `cmake/` directory.
The upstream recursive wildcard otherwise captures generated files below
`test_package/build`, changing the recipe revision after a consumer test.
An export regression check verifies that these generated files cannot change
the revision. This changes recipe packaging, not ncurses sources.

Export locally with:

```sh
CONAN_HOME=/path/to/personal-cache conan export conan/recipes/ncurses-wrynose/all --version 6.5 --user rstream --channel wrynose
```

Its focused consumer checks C++ header compatibility, the installed terminfo
database, window contents and input pushback. It uses CTest so ARM64 execution
can run through QEMU. Qualification must enable Conan's `can_run` explicitly;
unexecuted consumers do not qualify this exception.

The selected dependency revisions and deterministic export must be qualified
together before distribution. The v3 recipe extension prepares that
qualification; it does not certify an unbuilt SDK or package. Remove this
exception when the public recipe supports the same matrix.
The historical `../ncurses` recipe remains separate for existing package flows.
