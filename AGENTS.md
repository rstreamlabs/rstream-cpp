# Repository guidance

## Shared conventions

Follow the existing architecture, naming and coding style. Preserve public contracts, authentication, certificate verification and project policy. Keep changes in the component that owns the behavior.

Update user documentation and examples when behavior changes. Run the relevant checks; place regression tests with the owning component. Keep secrets, execution logs, investigation notes and standalone upstream reproductions outside the repository.

## Repository-specific guidance

Follow `CONTRIBUTING.md` and `docs/001-sdk-engineering.md`. Preserve Asio associated executors, allocators, cancellation slots and move-only handlers. Complete operations exactly once, avoid callbacks under locks and keep blocking work off event-loop threads. Preserve library/plugin topology, public package constraints and supported toolchain profiles.
