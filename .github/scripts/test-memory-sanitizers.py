"""Run native memory checks with a consistently instrumented dependency graph."""

import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess


def run(command, environment, timeout, stdout=None):
    timeout_tool = shutil.which(
        "gtimeout" if platform.system() == "Darwin" else "timeout"
    )
    if timeout_tool is None:
        raise RuntimeError("Memory checks require GNU coreutils timeout")
    subprocess.run(
        [timeout_tool, "--signal=TERM", "--kill-after=10s", str(timeout), *command],
        env=environment,
        stdout=stdout,
        check=True,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--jobs", type=int, choices=range(1, 17), default=2)
    parser.add_argument("--shared", action="store_true")
    parser.add_argument("--dynamic-plugins", action="store_true")
    args = parser.parse_args()
    if platform.system() not in ("Linux", "Darwin"):
        parser.error("Memory checks require Linux or macOS")
    source = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    command = [
        "conan",
        "install",
        str(source),
        "--build=missing",
        "--output-folder=" + str(output / "dependencies"),
        "-s",
        "build_type=Debug",
        "-s",
        "compiler.cppstd=20",
        "-o",
        "enable_testing=True",
        "-o",
        "enable_strict_warnings=True",
        "-o",
        "warnings_as_errors=True",
        "-o",
        "shared=" + str(args.shared),
        "-o",
        "static_plugins=" + str(not args.dynamic_plugins),
        "-c:h",
        "tools.build:jobs=" + str(args.jobs),
        "--format=json",
    ]
    configurations = {
        "tools.build:cflags": ["-fsanitize=address", "-fno-omit-frame-pointer"],
        "tools.build:cxxflags": ["-fsanitize=address", "-fno-omit-frame-pointer"],
        "tools.build:sharedlinkflags": ["-fsanitize=address"],
        "tools.build:exelinkflags": ["-fsanitize=address"],
    }
    configurations["tools.info.package_id:confs"] = list(configurations)
    for name, value in configurations.items():
        command.extend(["-c:h", name + "=" + json.dumps(value)])
    with (output / "dependencies.json").open("w") as graph:
        # Third-party build tools keep process caches; LSan is mandatory at runtime.
        run(
            command,
            environment | {"ASAN_OPTIONS": "detect_leaks=0:halt_on_error=1"},
            5400,
            graph,
        )
    toolchains = list((output / "dependencies").rglob("conan_toolchain.cmake"))
    if len(toolchains) != 1:
        raise RuntimeError("Expected exactly one instrumented Conan toolchain")
    build = output / "build"
    flags = "-fsanitize=address,undefined"
    run(
        [
            "cmake",
            "-S",
            str(source),
            "-B",
            str(build),
            "-G",
            "Ninja",
            "-DCMAKE_TOOLCHAIN_FILE=" + str(toolchains[0]),
            "-DCMAKE_BUILD_TYPE=Debug",
            "-DCMAKE_C_FLAGS=" + flags + " -fno-omit-frame-pointer",
            "-DCMAKE_CXX_FLAGS=" + flags + " -fno-omit-frame-pointer",
            "-DCMAKE_EXE_LINKER_FLAGS=" + flags,
            "-DCMAKE_SHARED_LINKER_FLAGS=" + flags,
            "-DCMAKE_MODULE_LINKER_FLAGS=" + flags,
            "-DRSTREAM_TEST_TIMEOUT_SCALE=2",
            "-DRSTREAM_TEST_TIMEOUT_SECONDS=300",
        ],
        environment,
        180,
    )
    run(
        ["cmake", "--build", str(build), "--parallel", str(args.jobs)],
        environment,
        1800,
    )
    detect_leaks = "1" if platform.system() == "Linux" else "0"
    run(
        [
            "ctest",
            "--test-dir",
            str(build),
            "--output-on-failure",
            "--output-junit",
            str(output / "results.junit.xml"),
        ],
        environment
        | {
            "ASAN_OPTIONS": "detect_leaks=" + detect_leaks + ":halt_on_error=1",
            "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1",
        },
        1800,
    )


if __name__ == "__main__":
    main()
