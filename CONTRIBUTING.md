# Contributing

Thanks for considering a contribution to `rstream-cpp`.

## Before opening a change

Small fixes can usually go straight to a pull request. For larger changes to the public API, transport model, packaging flow, or bundled tools, start with an issue or discussion first so the direction can be reviewed before implementation.

The [roadmap](roadmap/) records remaining native SDK and toolchain work.

## Local development

Follow [Build and develop the SDK](docs/007-building-from-source.md) for the compiler, Conan, CMake and Ninja prerequisites and a native checkout build. That path runs the enabled local tests and explains where the output is written. Use the published-package [application guide](docs/000-getting-started.md) when testing consumption separately from library development.

Cross-compilation, distributable packages and the required library/plugin matrix follow the [engineering contract](docs/001-sdk-engineering.md). They are additional qualification paths, not prerequisites for the first native build.

If a change affects examples, documentation, packaging metadata, or generated files, update the corresponding files in the same pull request.

Public pull requests do not automatically run the repository release/build workflows. Maintainers run CI after reviewing the change; include the local commands you ran in the PR description.

Changes to asynchronous code, dependencies, plugins, build profiles, or
packaging must satisfy the complete
[SDK engineering contract](./docs/001-sdk-engineering.md). It defines the
supported platform and topology matrix, Conan dependency policy, constrained
runtime requirements, and release validation gates.

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

WebTTY changes must preserve protocol-bound endpoint proofs and workspace credential verification without runtime control-plane calls. Review authentication and cryptographic changes with shared Go/JavaScript/C++ vectors and the affected native runtime combinations. Keep security audit results with release artifacts, outside the source tree.

Please do not disclose vulnerabilities in public issues. See [SECURITY.md](./SECURITY.md) for the reporting process used by this repository.
