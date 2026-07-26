import glob
import os
import re
import stat

import requests
from conan import ConanFile
from conan.tools import files
from conan.tools.files import symlinks
from toolchain_environment import compiler_executables, normalize_compiler_environment


class YoctoToolchainConan(ConanFile):
    name = "yocto-toolchain"
    exports = "toolchain_environment.py"
    settings = "os", "arch"
    options = {
        "arch": ["ANY"],
        "libc": ["glibc", "musl"],
    }
    upload_policy = "skip"

    @property
    def rstream_channel(self):
        return self.channel or "stable"

    @property
    def rstream_url(self):
        if self.rstream_channel == "stable":
            return "https://rstream.io"
        return "https://dev.rstream.io"

    @staticmethod
    def packages_from_response(data):
        if isinstance(data, dict) and "packages" in data:
            return data["packages"]
        return data

    @property
    def rstream_package(self):
        arch = str(self.settings.arch)
        if arch == "armv8":
            arch = "aarch64"
        elif arch != "x86_64":
            raise ValueError(f"Unsupported architecture: {arch}")
        operating_system = str(self.settings.os)
        if operating_system == "Linux":
            operating_system = "linux"
        else:
            raise ValueError(f"Unsupported operating system: {operating_system}")
        response = requests.get(
            f"{self.rstream_url}/api/packages",
            params={
                "name": self.name,
                "version": self.version,
                "channel": self.rstream_channel,
                "os": operating_system,
                "arch": arch,
                "libc": str(self.options.libc),
                "targetArch": str(self.options.arch),
            },
            timeout=30,
        )
        response.raise_for_status()
        packages = self.packages_from_response(response.json())
        if not isinstance(packages, list):
            raise ValueError("Unexpected package API response")
        if len(packages) == 0:
            raise ValueError("No package found for the given version and architecture")
        if len(packages) > 1:
            raise ValueError("Multiple packages found for the given version and architecture")
        package = packages[0]
        return {
            "id": package["id"],
            "filename": package["filename"],
            "checksum": package["checksum"],
        }

    @property
    def env_vars(self):
        return [
            "AR",
            "ARCH",
            "AS",
            "CC",
            "CFLAGS",
            "CONFIG_SITE",
            "CONFIGURE_FLAGS",
            "CPP",
            "CPPFLAGS",
            "CXX",
            "CXXFLAGS",
            "GDB",
            "KCFLAGS",
            "LD",
            "LDFLAGS",
            "M4",
            "NM",
            "OBJCOPY",
            "OBJDUMP",
            "OPENSSL_CONF",
            "OPENSSL_ENGINES",
            "OPENSSL_MODULES",
            "PATH",
            "PKG_CONFIG_PATH",
            "PKG_CONFIG_SYSROOT_DIR",
            "RANLIB",
            "READELF",
            "SSL_CERT_FILE",
            "STRIP",
            "SYSROOT",
            "TARGET_PREFIX",
        ]

    def build(self):
        package = self.rstream_package
        files.get(
            self,
            f"{self.rstream_url}/api/packages/{package['id']}/download",
            filename=package["filename"],
            sha256=package["checksum"],
            keep_permissions=True,
            strip_root=True,
        )

    def package(self):
        filename = glob.glob(f"{self.name}-*.sh")[0]
        os.chmod(filename, os.stat(filename).st_mode | stat.S_IEXEC)
        self.run(f"./{filename} -y -d {self.package_folder} -S -R")
        symlinks.absolute_to_relative_symlinks(self, self.package_folder)
        symlinks.remove_external_symlinks(self, self.package_folder)
        symlinks.remove_broken_symlinks(self, self.package_folder)
        files.replace_in_file(
            self,
            os.path.join(self.package_folder, "relocate_sdk.sh"),
            self.package_folder,
            "${PACKAGE_FOLDER:?PACKAGE_FOLDER not defined}",
        )
        files.replace_in_file(
            self,
            os.path.join(self.package_folder, "relocate_sdk.sh"),
            "'",
            '"',
        )
        files.replace_in_file(
            self,
            glob.glob(os.path.join(self.package_folder, "environment-setup*"))[0],
            self.package_folder,
            "$PACKAGE_FOLDER",
        )

    @staticmethod
    def parse_env(env, key, value):
        while True:
            refs = re.findall(r"\$[a-zA-Z0-9_]+", value)
            if len(refs) == 0:
                break
            for ref in refs:
                value = re.sub(
                    rf"\{ref}",
                    env.get(ref[1:], ""),
                    value,
                )
        env[key] = value

    def read_env_file(self, env, env_setup_file):
        with open(env_setup_file, encoding="utf-8") as file:
            for line in file:
                match = re.search(r"export ([^=]+)=(.*)", line)
                if match:
                    self.parse_env(env, match.group(1), match.group(2).strip('"'))

    def read_env_file_list(self, env, env_setup_file_list):
        for env_setup_file in env_setup_file_list:
            self.read_env_file(env, env_setup_file)

    def set_env_var(self, key, value):
        if key not in self.env_vars:
            return
        if key == "PATH":
            prefix = os.path.join(self.package_folder, "")
            for path in value.split(":"):
                if path.startswith(prefix):
                    self.cpp_info.bindirs.append(path.removeprefix(prefix))
            return
        self.buildenv_info.define(key, value.strip())

    def package_info(self):
        relocate_sdk = os.path.join(self.package_folder, "relocate_sdk.sh")
        self.run(f"PACKAGE_FOLDER={self.package_folder} {relocate_sdk}")
        env = {"PACKAGE_FOLDER": self.package_folder}
        self.read_env_file_list(
            env,
            glob.glob(os.path.join(self.package_folder, "environment-setup*")),
        )
        self.read_env_file_list(
            env,
            glob.glob(
                os.path.join(
                    env["OECORE_TARGET_SYSROOT"],
                    "environment-setup.d/*.sh",
                )
            ),
        )
        self.read_env_file_list(
            env,
            glob.glob(
                os.path.join(
                    env["OECORE_NATIVE_SYSROOT"],
                    "environment-setup.d/*.sh",
                )
            ),
        )
        del env["CROSS_COMPILE"]
        env["SYSROOT"] = env["SDKTARGETSYSROOT"]
        target_arch = str(self.options.arch)
        if str(self.options.libc) == "musl" and target_arch in {
            "x86_i686",
            "x86_core2",
        }:
            for key, value in env.items():
                env[key] = value.replace(
                    "-fstack-protector-strong",
                    "-fno-stack-protector",
                )
        normalize_compiler_environment(env)
        for key, value in env.items():
            self.set_env_var(key, value)
        self.buildenv_info.command_not_found_handle = None
        self.conf_info.define(
            "tools.build:compiler_executables",
            compiler_executables(env),
        )
        self.conf_info.define("tools.build:sysroot", env["SYSROOT"])
        self.conf_info.define("tools.build.cross_building:can_run", False)
        self.conf_info.define(
            "tools.cmake.cmaketoolchain:system_name",
            "Linux",
        )
        self.conf_info.define(
            "tools.cmake.cmaketoolchain:system_version",
            self.version,
        )
        configure_flags = env["CONFIGURE_FLAGS"]
        host_triplet = re.search(r"--host=([\w-]+)", configure_flags)
        build_triplet = re.search(r"--build=([\w-]+)", configure_flags)
        if host_triplet:
            self.conf_info.define("tools.gnu:host_triplet", host_triplet.group(1))
        if build_triplet:
            self.conf_info.define("tools.gnu:build_triplet", build_triplet.group(1))
