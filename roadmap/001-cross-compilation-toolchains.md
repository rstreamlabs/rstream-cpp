# Refresh cross-compilation toolchains

The Android, Emscripten and MinGW build profiles still use older compiler/SDK
baselines. Upgrade each SDK together with its Conan profile, rebuild the native
dependencies and run the resulting programs on the target platform. Refresh the
legacy VS Code development container in the same dedicated build-tooling pass.

The normal Linux/macOS/Windows Conan package checks do not qualify these separate
cross-compilation images. Yocto is handled independently.
