#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""Conan recipe package for rstream-cpp
"""

import os

from conan import ConanFile
import conan.tools.build
import conan.tools.cmake
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
        "deploy_python_dependencies" : [True, False],
        "deploy_python_stdlib" : [True, False],
        "enable_testing": [True, False],
        "ncurses_ref": [None, "ANY"],
        "protobuf_ref": [None, "ANY"],
        "shared": [True, False],
        "ssl_provider": ["openssl", "libressl"],
        "static_libstdcxx": [True, False],
        "with_maxminddb": [True, False],
        "with_ncurses": [True, False],
        "with_python": [True, False],
    }
    default_options = {
        "boost_ref": None,
        "build_bins": True,
        "build_arch": None,
        "build_channel": None,
        "build_os": None,
        "ncurses_ref": None,
        "protobuf_ref": "protobuf/7.35.0",
        "shared": False,
        "ssl_provider": "openssl",
        "static_libstdcxx": False,
        "with_python": False,
    }
    exports = \
        "version.txt"
    exports_sources = \
        "!.devcontainer", \
        "!.env*", \
        "!.git", \
        "!.github", \
        "!.vscode", \
        "!**/__pycache__", \
        "!**/.DS_Store", \
        "!**/*.pyc", \
        "!build*", \
        "!CMakeUserPresets.json", \
        "!conan", \
        "!out", \
        "!test_package/build", \
        "!test_package/CMakeUserPresets.json", \
        "!xcode", \
        "*"
    requires = \
        "docopt.cpp/[>=0.6.3]"
        
    @property
    def cmake_options(self):
        return {
            "build_bins": "BUILD_BINS",
            "deploy_python_dependencies": "PYTHON_INSTALL_DEPENDENCIES",
            "enable_testing": "ENABLE_TESTING",
            "static_libstdcxx": "STATIC_LIBSTDCXX",
            "with_maxminddb": "WITH_MAXMINDDB",
            "with_ncurses": "WITH_NCURSES",
            "with_python": "BUILD_BINDING_PYTHON",
        }

    def git(self):
        return conan.tools.scm.Git(self, folder=self.recipe_folder)
    
    def get_git_tag(self):
        try:
            return self.git().run("describe --tags --exact-match 2>/dev/null")
        except:
            return None

    def get_git_branch(self):
        event_name = os.getenv("GITHUB_EVENT_NAME")
        if (event_name and event_name == "pull_request"):
            return os.getenv("GITHUB_BASE_REF")
        else:
            return self.git().run("rev-parse --abbrev-ref HEAD")

    def get_version(self):
        version = os.getenv("VERSION")
        if version:
            return version
        tag, branch = self.get_git_tag(), self.get_git_branch()
        return tag if tag else branch

    def set_version(self):
        self.version = self.get_version()

    def config_options(self):
        if self.options.deploy_python_dependencies == None:
            self.options.deploy_python_dependencies = self.options.with_python
        if self.options.deploy_python_stdlib == None:
            self.options.deploy_python_stdlib = self.options.with_python
        if self.options.enable_testing == None:
            self.options.enable_testing = not conan.tools.build.cross_building(self)
        if self.options.with_maxminddb == None:
            self.options.with_maxminddb = self.settings.os != "Android"
        if self.options.with_ncurses == None:
            self.options.with_ncurses = self.settings.os != "Windows"
        if self.options.with_python == None:
            self.options.with_python = not conan.tools.build.cross_building(self)

    def configure(self):
        self.options["boost"].without_url = False
        if self.settings.os == "Emscripten":
            self.options["boost"].header_only = True
        if self.settings.compiler == "msvc":
            self.options["docopt.cpp"].boost_regex = True
        if self.options.with_python:
            self.options["boost"].without_python = False
        if self.options.with_ncurses:
            self.options["ncurses"].with_hashed_db = True

    def build_requirements(self):
        if self.options.enable_testing:
            self.test_requires("gtest/1.17.0")
        protobuf_ref = str(self.options.get_safe("protobuf_ref") or "").strip()
        if protobuf_ref and protobuf_ref != "none":
            self.build_requires(protobuf_ref)

    def requirements(self):
        boost_ref = str(self.options.get_safe("boost_ref") or "").strip()
        if boost_ref:
            self.requires(boost_ref, transitive_headers=True, transitive_libs=True)
        else:
            self.requires("boost/[>=1.83.0]", transitive_headers=True, transitive_libs=True)
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
        if self.options.with_maxminddb:
            self.requires("libmaxminddb/[>=1.9.1]", transitive_headers=True, transitive_libs=True)
        if self.options.with_ncurses:
            ncurses_ref = str(self.options.get_safe("ncurses_ref") or "").strip()
            if ncurses_ref:
                self.requires(ncurses_ref, transitive_headers=True, transitive_libs=True)
            else:
                self.requires("ncurses/[>=6.5]", transitive_headers=True, transitive_libs=True)
        
    def generate(self):
        cmake_toolchain = conan.tools.cmake.CMakeToolchain(self, generator="Ninja")
        cmake_toolchain.variables["CMAKE_VERBOSE_MAKEFILE"] = "ON"
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
        cmake_toolchain.variables["ENABLE_STATIC_PLUGINS"] = "OFF" if (self.settings.os == "Windows" and self.options.shared) else "ON"
        for key, value in self.cmake_options.items():
            cmake_toolchain.variables[value] = "ON" if self.options.get_safe(key, default=False) == True else "OFF"
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

    def test(self):
        cmake = conan.tools.cmake.CMake(self)
        cmake.test(output_on_failure=True)

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
