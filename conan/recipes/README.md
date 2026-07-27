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

## ncurses 6.5

This is a vendored Conan Center recipe used by the legacy cross-package flow.
It has no rstream-specific source patch. Treat it as a removal candidate:
validate the WebTTY Yocto glibc and musl packages against the public Conan
Center recipe, then remove the local export when those packages are
equivalent.

## Review Rule

Before every dependency update and release:

1. Compare each retained recipe with the matching Conan Center revision.
2. Confirm that every local change still serves a reproduced target failure.
3. Remove obsolete changes instead of carrying them forward.
4. Run the validation matrix in `docs/SDK_ENGINEERING.md`.

Do not add another local recipe without a reproduced packaging failure,
focused validation, and a smaller upstream-compatible alternative having been
ruled out.
