# Contributing

Thanks for considering a contribution to `rstream-cpp`.

## Before opening a change

Small fixes can usually go straight to a pull request. For larger changes to the public API, transport model, packaging flow, or bundled tools, start with an issue or discussion first so the direction can be reviewed before implementation.

## Local development checks

Before opening a pull request, run the main local checks:

```bash
conan profile detect --force
conan config install conan/config
conan create . -u --build=missing -pr:b default -pr:h default
```

If you already have the native dependencies installed locally, the direct CMake flow is:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

If you use the repository helper script, the equivalent flow is:

```bash
./build.sh
```

Use `build-conan-cross.sh` for distributable Linux, Windows, and macOS
profiles. Library linkage and plugin loading are independent dimensions:

```bash
OSS="linux" \
LINUX_ARCHS="x86_64" \
LINUX_TCLIBCS="glibc musl" \
LINUX_BUILD_SHARED="on off" \
LINUX_PLUGIN_MODES="static dynamic" \
./build-conan-cross.sh build
```

If a change affects examples, documentation, packaging metadata, or generated files, update the corresponding files in the same pull request.

Public pull requests do not automatically run the repository release/build workflows. Maintainers run CI after reviewing the change; include the local commands you ran in the PR description.

## Style

Keep changes focused, explicit, and idiomatic for the existing codebase.

- Prefer small pull requests over broad refactors.
- Keep public API and ABI changes intentional and documented.
- Preserve associated executors, allocators, cancellation slots, and move-only
  handlers in asynchronous APIs.
- Serialize mutable asynchronous state with an executor or strand. Use a mutex
  only for state also accessed synchronously.
- Never invoke external callbacks while holding a lock.
- Complete each asynchronous operation exactly once, including cancellation
  and destruction paths.
- Keep blocking I/O off Asio event-loop threads and bounded in time and memory.
- Handle partial I/O, malformed input, resource exhaustion, and repeated
  cancellation explicitly.
- Add deterministic failure and concurrency tests for changed state machines;
  do not rely on sleeps for ordering.
- Update the README or `docs/` when user-facing behavior changes.

## Generated and packaged content

Some files are generated or copied into package outputs during the build and Conan flows. If you modify the source definitions or packaging inputs, regenerate or rebuild the corresponding outputs before opening the pull request.

## Security

Please do not disclose vulnerabilities in public issues. See [SECURITY.md](./SECURITY.md) for the reporting process used by this repository.
