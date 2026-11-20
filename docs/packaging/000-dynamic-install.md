# Stage an application with dynamic plugins

Build and test the application with the same SDK and modules you will
install. This example uses shared SDK libraries and dynamic modules on
macOS, with executables in `bin/`, libraries in `lib/` and plugins in
`lib/rstream/`. Use the [plugin guide](../009-plugins-and-packaging.md)
to decide whether your application needs this layout.

## Build and stage the SDK

Start from the [native build prerequisites](../001-building-from-source.md#prerequisites)
and its `rstream-local` profile. Run from the SDK checkout root. The Conan
runtime deployer collects the shared dependencies from the same graph
used to build the SDK. The explicit toolchain path lets this build coexist
with the default native Debug preset.

```sh
dynamic_build="$PWD/build/dynamic-guide/build/Debug"
runtime_dir="$PWD/build/dynamic-runtime"
stage="$PWD/build/dynamic-stage"
conan install . --output-folder=build/dynamic-guide --build=missing \
  -pr:b rstream-local -pr:h rstream-local -s:b compiler.cppstd=20 \
  -s:h compiler.cppstd=20 -s:h build_type=Debug \
  -o '&:shared=True' -o '&:static_plugins=False' -o '&:enable_testing=False' \
  -o '&:with_ncurses=False' -o '&:with_maxminddb=False' \
  -o 'boost/*:without_cobalt=True' -c:h tools.cmake.cmaketoolchain:user_presets= \
  --deployer=runtime_deploy --deployer-folder="$runtime_dir"
cmake -S . -B "$dynamic_build" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE="$dynamic_build/generators/conan_toolchain.cmake" \
  -DCMAKE_INSTALL_LIBDIR=lib \
  -DBUILD_DEMOS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_BENCHMARKS=OFF
cmake --build "$dynamic_build"
cmake --install "$dynamic_build" --prefix "$stage"
for library in "$runtime_dir"/*.dylib*; do
  cp -P "$library" "$stage/lib/" || exit
done
```

Use a fresh staging prefix for each build you distribute. Preserve library
symlinks. The SDK install provides its own libraries and modules, and the
copy step adds the third-party dylibs. Files such as `protoc` in the
deployer output are unnecessary for this runtime. On Linux the shared
libraries have `.so` names. Verify the corresponding ELF loader paths
with `ldd` and `readelf`. On Windows, stage the applicable DLLs with the
application and check their imports with `dumpbin /DEPENDENTS`.

## Install the application beside the modules

Keep the application's usual [SDK link and plugin setup](../009-plugins-and-packaging.md#choose-the-transports).
For this macOS layout, add these rules to its CMake target.

```cmake
set_target_properties(my_app PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  INSTALL_RPATH "@executable_path/../lib")
install(TARGETS my_app RUNTIME DESTINATION bin)
```

The output directory isolates build-tree plugin copies. Configure the
application with the staged SDK and the same Conan toolchain.
Set `app_source` to the directory containing its `CMakeLists.txt`.

```sh
app_source=../my-app
app_build="$PWD/build/dynamic-app"
cmake -S "$app_source" -B "$app_build" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE="$dynamic_build/generators/conan_toolchain.cmake" \
  -DCMAKE_PREFIX_PATH="$stage"
cmake --build "$app_build"
cmake --install "$app_build" --prefix "$stage"
```

The install RPATH lets the app find staged libraries relative to
itself. On Linux the equivalent is `$ORIGIN/../lib`.

## Check the installed runtime

From the SDK checkout, use a shell without the Conan runtime environment.
On macOS, inspect the staged application and modules, then run the inspector.

```sh
stage="$PWD/build/dynamic-stage"
otool -L "$stage/bin/my_app" "$stage/lib/rstream/"*
"$stage/bin/rstream-inspect" plugins
```

The inspector should show both plugins with `type` set to `dynamic`
and paths under `lib/rstream/`. Confirm the registered
`io.stream.tcp` and `io.stream.rstrm` elements. A zero exit status alone
does not confirm either plugin was found. A Release deployment can use
the application for the runtime test without shipping the Debug inspector.

The last check is a stream exchange. Point the installed application's
TCP and rstream modes at a local listener and a private tunnel. The
[loopback and private relay examples](../tools/001-streams.md) provide
the two endpoints and expected response. The installed `rstream-ncat`
can also check both paths. Run the same smoke tests on the oldest
target system you support.
