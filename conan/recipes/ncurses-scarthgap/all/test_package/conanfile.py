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
        if can_run(self) and cross_building(self) and self.settings.arch == "armv8":
            qemu = shutil.which("qemu-aarch64")
            sysroot = self.conf.get("tools.build:sysroot")
            if not qemu or not sysroot:
                raise ConanInvalidConfiguration("ARM64 runtime qualification requires QEMU and the SDK sysroot")
            toolchain.variables["CMAKE_CROSSCOMPILING_EMULATOR"] = ";".join([qemu, "-L", sysroot])
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
