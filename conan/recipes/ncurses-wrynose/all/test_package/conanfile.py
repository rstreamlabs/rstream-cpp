from pathlib import Path
import shutil

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.build import can_run, cross_building
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout


class TestPackage(ConanFile):
    settings = "os", "arch", "compiler", "build_type"
    test_type = "explicit"

    def requirements(self):
        self.requires(self.tested_reference_str)

    def layout(self):
        cmake_layout(self)

    def generate(self):
        toolchain = CMakeToolchain(self)
        toolchain.variables["TEST_FULLY_STATIC"] = str(self.settings.get_safe("os.sdk", "")).endswith("-musl")
        if can_run(self) and cross_building(self) and str(self.settings.arch) in ("armv8", "x86_64"):
            emulator = "qemu-aarch64" if self.settings.arch == "armv8" else "qemu-x86_64"
            qemu = shutil.which(emulator)
            sysroot = self.conf.get("tools.build:sysroot")
            if not qemu or not sysroot:
                raise ConanInvalidConfiguration(f"Runtime qualification requires {emulator} and the SDK sysroot")
            runner = [qemu]
            if str(self.settings.get_safe("os.sdk", "")) in (
                    "yocto-toolchain-6.0.3-x86_64_v2-musl",
                    "yocto-toolchain-6.0.3-x86_64_v2-glibc"):
                runner += ["-cpu", "Nehalem"]
            elif str(self.settings.get_safe("os.sdk", "")) in (
                    "yocto-toolchain-6.0.3-x86_64_v3-musl",
                    "yocto-toolchain-6.0.3-x86_64_v3-glibc"):
                runner += ["-cpu", "Haswell,-hle,-rtm"]
            runner += ["-L", sysroot]
            if str(self.settings.get_safe("os.sdk", "")).endswith("-glibc"):
                root = Path(sysroot).resolve()
                loaders = {path.resolve() for directory in ("lib", "lib64")
                           for path in (root / directory).glob("ld*.so*") if path.is_file()}
                if len(loaders) != 1 or not all(path.is_relative_to(root) for path in loaders):
                    raise ConanInvalidConfiguration("Expected exactly one dynamic loader inside the SDK sysroot")
                libraries = ":".join(str(root / directory)
                                     for directory in ("lib", "usr/lib", "lib64", "usr/lib64"))
                runner += [str(next(iter(loaders))), "--inhibit-cache", "--library-path", libraries]
            toolchain.variables["CMAKE_CROSSCOMPILING_EMULATOR"] = ";".join(runner)
        toolchain.generate()
        CMakeDeps(self).generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def test(self):
        if can_run(self):
            CMake(self).ctest(cli_args=["--output-on-failure", "--no-tests=error"])
        else:
            self.output.warning("Runtime qualification was not performed: can_run is false")
