# Cross-compilation

A cross build runs on a build machine and produces code for a different target.
The build profile selects tools that execute during compilation; the host
profile selects the application's CPU, operating system, compiler and ABI.

Start with a working [native source build](001-building-from-source.md). Choose
a host profile matching the device's toolchain and dependency graph. Repository
profiles are in [`conan/config/profiles`](../conan/config/profiles); distribution
builds configure them through `build-conan-cross.sh` and its selected SDK.

| Input                         | Match it to                                                 |
| ----------------------------- | ----------------------------------------------------------- |
| CPU and compiler              | Device architecture, compiler ABI and C++20 support.        |
| libc / system SDK             | The target's runtime and deployment baseline.               |
| Conan host profile            | Target settings and cross-compilation toolchain.            |
| Library/plugin options        | The intended static/shared deployment.                      |
| TLS and protobuf dependencies | Headers, generated code and libraries built for the target. |

## Build and run on the target

Use separate Conan build and host profiles when adapting the
[native commands](001-building-from-source.md). Keep the selected profiles,
settings and options consistent across install, configure, build and package.
The distribution script's [release-equivalent command](014-sdk-engineering.md#distribution-packaging)
covers the maintained multi-platform package flow.

Copy the resulting application, libraries and plugins to the device and run a
representative connection with verified TLS, payload exchange and shutdown.
Test queue/buffer exhaustion and cancellation under the device's resource limits.
The [supported-environment contract](014-sdk-engineering.md#supported-environments)
and [toolchain roadmap](../roadmap/001-cross-compilation-toolchains.md) identify
the maintained baselines and profiles that need further work.

Keep public Conan dependencies for ordinary applications. Local recipes are
restricted packaging overrides; their purpose and scope are in the
[recipe inventory](../conan/recipes/README.md).
