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

If a change affects examples, documentation, packaging metadata, or generated files, update the corresponding files in the same pull request.

Public pull requests do not automatically run the repository release/build workflows. Maintainers run CI after reviewing the change; include the local commands you ran in the PR description.

## Style

Keep changes focused, explicit, and idiomatic for the existing codebase.

- prefer small pull requests over broad refactors
- keep public API changes intentional and documented
- update the README or `docs/` when user-facing behavior changes
- avoid mixing unrelated cleanups with behavioral changes

## Generated and packaged content

Some files are generated or copied into package outputs during the build and Conan flows. If you modify the source definitions or packaging inputs, regenerate or rebuild the corresponding outputs before opening the pull request.

## Security

Please do not disclose vulnerabilities in public issues. See [SECURITY.md](./SECURITY.md) for the reporting process used by this repository.
