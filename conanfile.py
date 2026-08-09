#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""Conan recipe package for rstream-cpp
"""

import os

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
import conan.tools.build
import conan.tools.cmake
import conan.tools.env
import conan.tools.files
import conan.tools.scm


class ConanPackage(ConanFile):
    name = "rstream"
    license = "Apache-2.0"
    url = "https://github.com/rstreamlabs/rstream-cpp"
    description = "C++ SDK for rstream - serverless networking"
    settings = "os", "compiler", "build_type", "arch"
    options = {
        "boost_ref": [None, "ANY"],
        "build_bins": [True, False],
        "build_arch": [None, "ANY"],
        "build_channel": [None, "ANY"],
        "build_os": [None, "ANY"],
        "deploy_python_dependencies": [None, True, False],
        "deploy_python_stdlib": [None, True, False],
        "enable_strict_warnings": [True, False],
        "enable_testing": [None, True, False],
        "ncurses_ref": [None, "ANY"],
        "protobuf_ref": [None, "ANY"],
        "shared": [True, False],
        "ssl_provider": ["openssl", "libressl"],
        "static_plugins": [None, True, False],
        "static_libstdcxx": [True, False],
        "with_maxminddb": [None, True, False],
        "with_ncurses": [None, True, False],
        "with_python": [True, False],
        "warnings_as_errors": [True, False],
    }
    default_options = {
        "boost_ref": None,
        "build_bins": True,
        "build_arch": None,
        "build_channel": None,
        "build_os": None,
        "deploy_python_dependencies": None,
        "deploy_python_stdlib": None,
        "enable_strict_warnings": False,
        "enable_testing": None,
        "ncurses_ref": None,
        "protobuf_ref": "protobuf/7.35.0",
        "shared": False,
        "ssl_provider": "openssl",
        "static_plugins": None,
        "static_libstdcxx": False,
        "with_maxminddb": None,
        "with_ncurses": None,
        "with_python": False,
        "warnings_as_errors": False,
    }
    exports = "version.txt"
    exports_sources = (
        "!.devcontainer",
        "!.env*",
        "!.git",
        "!.github",
        "!.vscode",
        "!**/__pycache__",
        "!**/.DS_Store",
        "!**/*.gcda",
        "!**/*.gcno",
        "!**/*.gcov",
        "!**/*.log",
        "!**/*.profdata",
        "!**/*.profraw",
        "!*.gcda",
        "!*.gcno",
        "!*.gcov",
        "!*.profdata",
        "!*.profraw",
        "!**/*.pyc",
        "!build*",
        "!CMakeUserPresets.json",
        "!conan/compose.yaml",
        "!conan/config",
        "!conan/Dockerfile",
        "!conan/recipes",
        "!conan/update-conan-profile.sh",
        "!out",
        "!test_package/build*",
        "!test_package/CMakeUserPresets.json",
        "!xcode",
        "*",
    )
    requires = "docopt.cpp/[>=0.6.3]"

    @property
    def cmake_options(self):
        return {
            "build_bins": "BUILD_BINS",
            "deploy_python_dependencies": "PYTHON_INSTALL_DEPENDENCIES",
            "enable_strict_warnings": "ENABLE_STRICT_WARNINGS",
            "enable_testing": "ENABLE_TESTING",
            "static_plugins": "ENABLE_STATIC_PLUGINS",
            "static_libstdcxx": "STATIC_LIBSTDCXX",
            "with_maxminddb": "WITH_MAXMINDDB",
            "with_ncurses": "WITH_NCURSES",
            "with_python": "BUILD_BINDING_PYTHON",
            "warnings_as_errors": "WARNINGS_AS_ERRORS",
        }

    def git(self):
        return conan.tools.scm.Git(self, folder=self.recipe_folder)

    def get_git_tag(self):
        try:
            return self.git().run("describe --tags --exact-match 2>/dev/null")
        except Exception:
            return None

    def get_version(self):
        version = os.getenv("VERSION")
        if version:
            return version
        tag = self.get_git_tag()
        if tag:
            return tag
        with open(os.path.join(self.recipe_folder, "version.txt"), encoding="utf-8") as version_file:
            return version_file.read().strip()

    def set_version(self):
        self.version = self.get_version()

    @staticmethod
    def option_enabled(value):
        return str(value).lower() == "true"

    @staticmethod
    def option_unset(value):
        return str(value).lower() == "none"

    def dependency_overrides(self):
        return {
            option: str(self.options.get_safe(option) or "").strip()
            for option in ("boost_ref", "ncurses_ref", "protobuf_ref")
        }

    def is_package_build(self):
        return all(
            str(self.options.get_safe(option) or "").strip()
            for option in ("build_os", "build_arch", "build_channel")
        )

    def validate_dependency_overrides(self):
        private_overrides = [
            reference for reference in self.dependency_overrides().values() if "@" in reference
        ]
        if private_overrides and not self.is_package_build():
            raise ConanInvalidConfiguration(
                "Private dependency overrides are reserved for distribution package builds. "
                "Use public Conan Center references or set build_os, build_arch, and build_channel."
            )

    def configure(self):
        if self.option_unset(self.options.deploy_python_dependencies):
            self.options.deploy_python_dependencies = self.options.with_python
        if self.option_unset(self.options.deploy_python_stdlib):
            self.options.deploy_python_stdlib = self.options.with_python
        if self.option_unset(self.options.enable_testing):
            self.options.enable_testing = not conan.tools.build.cross_building(self)
        if self.option_unset(self.options.static_plugins):
            self.options.static_plugins = not self.option_enabled(self.options.shared)
        if self.option_unset(self.options.with_maxminddb):
            self.options.with_maxminddb = self.settings.os != "Android"
        if self.option_unset(self.options.with_ncurses):
            self.options.with_ncurses = self.settings.os != "Windows"

        # Only constrain components that rstream actually requires. Optional Boost
        # component pruning belongs to the root build profile so this recipe remains
        # composable when an application also has a direct Boost requirement.
        self.options["boost"].without_url = False
        protobuf_ref = str(self.options.get_safe("protobuf_ref") or "").strip()
        if protobuf_ref and protobuf_ref != "none":
            shared_runtime = self.option_enabled(self.options.shared) or not self.option_enabled(
                self.options.static_plugins
            )
            self.options["abseil"].shared = shared_runtime
            self.options["protobuf"].shared = shared_runtime
            self.options["yaml-cpp"].shared = shared_runtime
        if self.settings.os == "Emscripten":
            self.options["boost"].header_only = True
        if self.option_enabled(self.options.with_python):
            self.options["boost"].without_python = False
        if self.option_enabled(self.options.with_ncurses):
            self.options["ncurses"].with_hashed_db = True

    def build_requirements(self):
        self.validate_dependency_overrides()
        protobuf_ref = str(self.options.get_safe("protobuf_ref") or "").strip()
        if protobuf_ref and protobuf_ref != "none":
            self.build_requires(protobuf_ref)

    def requirements(self):
        self.validate_dependency_overrides()
        boost_ref = str(self.options.get_safe("boost_ref") or "").strip()
        if boost_ref:
            self.requires(
                boost_ref,
                transitive_headers=True,
                transitive_libs=True,
                force=True,
            )
        else:
            # Boost 1.91.0's Conan Center recipe expects boost_cobalt_io_ssl but
            # does not configure the OpenSSL dependency needed to build it.
            self.requires(
                "boost/[>=1.81.0 <1.91.0]",
                transitive_headers=True,
                transitive_libs=True,
            )
        self.requires("nlohmann_json/[>=3.11.2]", transitive_headers=True, transitive_libs=True)
        self.requires("spdlog/[>=1.12.0]", transitive_headers=True, transitive_libs=True)
        self.requires("yaml-cpp/[>=0.8.0]", transitive_headers=True, transitive_libs=True)
        if self.options.ssl_provider.value == "openssl":
            self.requires("openssl/[>=3.1.2 <4]", transitive_headers=True, transitive_libs=True)
        if self.options.ssl_provider.value == "libressl":
            self.requires("libressl/[>=3.9.1]", transitive_headers=True, transitive_libs=True)
        protobuf_ref = str(self.options.get_safe("protobuf_ref") or "").strip()
        if protobuf_ref and protobuf_ref != "none":
            self.requires(protobuf_ref, transitive_headers=True, transitive_libs=True)
        if self.option_enabled(self.options.with_maxminddb):
            self.requires("libmaxminddb/[>=1.9.1]", transitive_headers=True, transitive_libs=True)
        if self.option_enabled(self.options.with_ncurses):
            ncurses_ref = str(self.options.get_safe("ncurses_ref") or "").strip()
            if ncurses_ref:
                self.requires(ncurses_ref, transitive_headers=True, transitive_libs=True)
            else:
                self.requires("ncurses/[>=6.5]", transitive_headers=True, transitive_libs=True)

    def validate(self):
        self.validate_dependency_overrides()
        if self.option_enabled(self.options.static_libstdcxx) and not self.option_enabled(
            self.options.static_plugins
        ):
            raise ConanInvalidConfiguration(
                "A fully static C++ runtime is incompatible with dynamically loaded plugins. "
                "Disable static_libstdcxx or enable static_plugins."
            )

    def generate(self):
        cmake_toolchain = conan.tools.cmake.CMakeToolchain(self, generator="Ninja")
        build_channel = str(self.options.get_safe("build_channel") or "").strip()
        build_os = str(self.options.get_safe("build_os") or "").strip()
        build_arch = str(self.options.get_safe("build_arch") or "").strip()
        if build_channel:
            cmake_toolchain.variables["RSTREAM_BUILD_CHANNEL"] = build_channel
        if build_os:
            cmake_toolchain.variables["RSTREAM_BUILD_OS"] = build_os
        if build_arch:
            cmake_toolchain.variables["RSTREAM_BUILD_ARCH"] = build_arch
        if self.settings.os == "Linux":
            cmake_toolchain.variables["DEAD_CODE_ELIMINATION"] = "ON"
        for key, value in self.cmake_options.items():
            enabled = self.option_enabled(self.options.get_safe(key, default=False))
            cmake_toolchain.variables[value] = "ON" if enabled else "OFF"
        cmake_toolchain.variables["SSL_PROVIDER"] = self.options.ssl_provider
        cmake_toolchain.generate()
        cmake_deps = conan.tools.cmake.CMakeDeps(self)
        cmake_deps.generate()

    def layout(self):
        conan.tools.cmake.cmake_layout(self)

    def build(self):
        cmake = conan.tools.cmake.CMake(self)
        cmake.configure()
        cmake.build()
        if self.option_enabled(self.options.enable_testing):
            test_environment = conan.tools.env.Environment()
            test_environment.define("CTEST_OUTPUT_ON_FAILURE", "1")
            with test_environment.vars(self).apply():
                cmake.test()

    def package(self):
        cmake = conan.tools.cmake.CMake(self)
        # 'install/strip' target is known to have issues on windows with absolute destination
        if self.settings.os == "Windows":
            cmake.install()
        else:
            cmake.build(target="install/strip")
        conan.tools.files.copy(self, "LICENSE", self.build_folder, os.path.join(self.package_folder, "licenses"))

    def package_info(self):
        self.cpp_info.set_property("cmake_find_mode", "none")
        self.cpp_info.builddirs.append(os.path.join("lib", "cmake", self.name))
        if not self.option_enabled(self.options.static_plugins):
            self.runenv_info.prepend_path(
                "LD_LIBRARY_PATH", os.path.join(self.package_folder, "lib")
            )
            self.runenv_info.prepend_path(
                "DYLD_LIBRARY_PATH", os.path.join(self.package_folder, "lib")
            )
            self.runenv_info.prepend_path(
                "PATH", os.path.join(self.package_folder, "bin")
            )
