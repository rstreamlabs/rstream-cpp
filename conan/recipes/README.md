# Local Conan Recipes

Public `rstream` packages depend only on recipes published by Conan Center.
The recipes in this directory are private build inputs used by
`build-conan-cross.sh`; they must never appear in the dependency graph exposed
to SDK consumers.

## Boost 1.85.0

The recipe is based on the matching Conan Center recipe. Its rstream-specific
changes are limited to:

- propagating `CFLAGS`, `CXXFLAGS`, and `LDFLAGS` from the Conan build
  environment to Boost.Build, as required by the configured Yocto SDK;
- selecting the GNU Clang Boost.Build toolset and library naming convention
  for LLVM-MinGW cross builds;
- tolerating an unset `fPIC` option and correcting the `arch` package-property
  key used by the vendored recipe.

Keep this override only while the matching Conan Center recipe cannot build
the supported Yocto and LLVM-MinGW package matrix without these changes.
Validate removal with the historical cross-package command and the Windows
static/shared plugin matrix before deleting it.

Wrynose 6.0 and native macOS packaging now use unmodified public Boost. The
historical override is selected only for the remaining legacy Linux and
LLVM-MinGW flows while those toolchains await their own qualification.

## ncurses 6.5

This is a vendored Conan Center recipe used by the legacy cross-package flow.
It has no rstream-specific source patch, but it does have recipe changes:
hashed terminfo database support, a native ncurses tool requirement when cross
compiling, and removal of the public recipe's ARM cross-build rejection.

The Wrynose review found that the public 6.5 recipe revision
`d35d5bc6bbe86cf25c10957df6302f8f` still rejects cross-building to or from ARM
in `validate()`. This must be resolved before removing the packaging override
for ARM targets. It must not be bypassed by pretending the build is native.

GCC 15 also defaults to C23, exposing a separate ncurses 6.5 configure bug:
it generates a `bool` macro that breaks C++ standard headers. The public recipe
already selects C17 for GCC 15. The SDK's environment previously overwrote
that flag after Autotools generated it; the Wrynose tool recipe now appends its
flags instead. No ncurses recipe change is required for that issue. Validate
the public x86_64 recipe on both libcs before changing packaging defaults.

That x86_64 validation passed on musl and glibc. Wrynose packaging now selects
the public recipe on x86_64 and only the narrower
[`ncurses-wrynose`](ncurses-wrynose/README.md) exception on ARM64. Native macOS
uses public recipes as well. `USE_PATCHED_CONAN_DEPS=off` disables all private
exports and overrides. The local qualification driver requires the explicit
`--arm-ncurses-exception` option for its ARM64 distribution-package checks.

## Review Rule

Before every dependency update and release:

1. Compare each retained recipe with the matching Conan Center revision.
2. Confirm that every local change still serves a reproduced target failure.
3. Remove obsolete changes instead of carrying them forward.
4. Run the validation matrix in `docs/001-sdk-engineering.md`.

Do not add another local recipe without a reproduced packaging failure,
focused validation, and a smaller upstream-compatible alternative having been
ruled out.
