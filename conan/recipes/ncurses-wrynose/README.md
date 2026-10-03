# Wrynose ARM64 packaging exception

This candidate is for compiled distribution packages only. Library consumers
must continue to use unmodified public Conan Center recipes.

The base is `ncurses/6.5#d35d5bc6bbe86cf25c10957df6302f8f` from
`https://center2.conan.io`. `upstream.json` records the downloaded file hashes;
`upstream.diff` records the complete recipe delta. No ncurses source is patched.

The public recipe rejects every cross-build to or from ARM before compilation.
The exception permits only Linux x86_64 to Linux ARM64 using Yocto 6.0.x, while
preserving the rejection for other configurations. Upstream's native build
compiler detection is retained and must be verified in the build log.

The first ARM64 build compiled successfully, then installation failed because
`install -s` invoked the workstation's x86_64 `strip` on an ARM64 `tic`. The
recipe disables install-time stripping for cross builds; ncurses source and
compiler flags remain unchanged. Final distribution stripping uses target
tools.

Export locally with:

```sh
conan export conan/recipes/ncurses-wrynose/all --version 6.5 --user rstream --channel wrynose
```

Its focused consumer checks C++ header compatibility, the installed terminfo
database, window contents and input pushback. It uses CTest so ARM64 execution
can run through QEMU. Qualification must enable Conan's `can_run` explicitly;
unexecuted consumers do not qualify this exception.

This remains a candidate until both ARM64 libc configurations and their rstream
package tests pass. Remove it when the public recipe supports the same matrix.
The historical `../ncurses` recipe remains separate for existing package flows.
