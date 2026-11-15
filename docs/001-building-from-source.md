# Build and develop the C++ SDK

This guide compiles the SDK in your checkout so that edits can be rebuilt and tested locally. To add a released SDK to an application instead, use [Build an application](000-getting-started.md). Neither compilation nor the local library tests require a running engine or rstream credentials.

Conan resolves third-party libraries and generates the CMake toolchain. CMake then builds this checkout with Ninja. The native development build includes the SDK, companion tools and their tests; cross-compilation and distribution packaging are separate paths in the [engineering guide](006-sdk-engineering.md).

## Prerequisites

| Tool | Requirement |
| --- | --- |
| C++ compiler and platform SDK | C++20 support. Use GCC or Clang on Linux, Xcode Command Line Tools on macOS, or Visual Studio 2022 with Desktop development with C++ on Windows. |
| Python and Conan | Conan 2.34.0, the version used by the repository workflows. Install it in a Python environment appropriate to the host. |
| CMake and Ninja | CMake 3.23 or newer for the generated user presets, and Ninja on `PATH`. The Conan recipe selects Ninja explicitly. |
| Git | Clone the source and retain its version history. |
| Network access | Conan needs its configured remotes for dependencies not already present in the local cache. The native library path uses public Conan Center dependencies. |

On Windows, run the native build in a Developer PowerShell for Visual Studio so that the MSVC compiler and Windows SDK are available. On macOS, `xcode-select --install` installs the command-line compiler and platform tools.

Check the toolchain before building:

```sh
conan --version
cmake --version
ninja --version
```

## Configure a native checkout

Clone the repository, then run subsequent commands from its root:

```sh
git clone https://github.com/rstreamlabs/rstream-cpp.git
cd rstream-cpp
conan profile detect --name rstream-local --exist-ok
```

The named profile records the detected host compiler without replacing a default profile. If it already exists, `--exist-ok` preserves it. Use a different profile name when changing compilers or SDKs; use the same selection for installation and compilation.

Install the dependencies for the native Debug build with tests enabled:

```sh
conan install . --output-folder=build/native --build=missing -pr:b rstream-local -pr:h rstream-local -s:b compiler.cppstd=20 -s:h compiler.cppstd=20 -s:h build_type=Debug -o "&:enable_testing=True" -o "boost/*:without_cobalt=True" -c tools.build:jobs=4
```

This selects the complete native development configuration, including companion tools exercised by the tests, and preserves the recipe's default library/plugin linkage. Conan resolves the optional libraries enabled by that configuration. Missing dependency binaries may be compiled on the first run; that is separate from rebuilding the SDK after an edit. The job setting bounds build parallelism and can be adjusted to the machine.

The generated toolchain and dependency files remain under `build/native/`. This command consumes `conanfile.py` from the current directory; it does not download the released rstream SDK in place of your source.

## Compile and run tests

Conan generates a `conan-debug` CMake preset carrying the selected toolchain, dependencies and build environment:

```sh
cmake --preset conan-debug
cmake --build --preset conan-debug
ctest --preset conan-debug --output-on-failure --no-tests=error
```

The preset uses `build/native/build/Debug/`. Its test environment includes the dependency runtime paths. A successful local build includes passing tests, not merely a generated project.

After editing source, rerun the build and test commands. Rerun `conan install` and configure when changing the dependency graph, compiler, build type or options. Keep different configurations in separate output folders. The generated `CMakeUserPresets.json` is local; do not commit machine-specific dependency paths.

A separate `conan build` invocation must repeat the same profiles, settings and options. It does not remember the preceding install command; omitting them can select the default Release graph instead of this Debug configuration. The generated CMake presets avoid that mismatch for local development.

| Result | Location or next action |
| --- | --- |
| Build and test output | `build/native/build/Debug/`; failures include the test output. |
| SDK libraries and plugins | Inside that build tree; their linkage follows the configured library/plugin options. |
| A package in the local Conan cache | Use `conan create` as described below. |
| A first application | Follow the [application guide](000-getting-started.md), selecting the intended SDK package. |

## Build a package or the utilities

`conan create .` builds and tests a package in Conan's local cache, rather than leaving an editable build in the checkout. Use the same profiles, settings and options as above when qualifying that configuration. The recipe's version comes from the selected source/tag or `version.txt`; an untagged package is not a published release.

The native configuration above includes companion tools. For a library-only consumer package, select `build_bins=False` and disable the tool-dependent test configuration rather than combining it with the full native test suite. The [tools guide](005-tools.md) describes the utilities; the [engineering guide](006-sdk-engineering.md) defines supported library/plugin combinations and platform requirements.

The `build.sh` helper is a direct CMake build/test/install path for an already provisioned dependency environment. It does not resolve the native dependencies in place of Conan. Use it only when the required CMake packages and toolchain are available. Follow [Contributing](../CONTRIBUTING.md) for the asynchronous API and change-review requirements.
