#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# See LICENSE file in the project root for license information.

import glob
import os
import shutil
import tarfile
import zipfile

"""Conan deployer for rstream packages.
"""
def get_packages(graph, conan_dependencies):
    def get_files(graph, conanfile, pattern):
        plugindir = os.path.join("lib", conanfile.ref.name)
        if conanfile.options.shared:
            if pattern != "*":
                graph.root.conanfile.output.warning("packaging shared libraries with pattern '" + pattern + "'")
            return [ (pattern, "bin", "bin"), ("*dylib" if conanfile.settings.os == "Macos" else "*.so*", "lib", "lib"), ("*.dll*" if conanfile.settings.os == "Windows" else "*.so*", plugindir, plugindir) ]
        else:
            return [ (pattern, "bin", "bin"), ("*.dll*" if conanfile.settings.os == "Windows" else "*.so*", plugindir, plugindir) ]
    packages = {
        "rstream-utils" : [
            { "rstream": lambda conanfile: get_files(graph, conanfile, "*") }
        ],
        "rstream-rtty" : [
            { "rstream": lambda conanfile: get_files(graph, conanfile, "rstream-rtty-*") }
        ]
    }
    result = { }
    for package, runtime_dependencies in packages.items():
        result[package] = { }
        for runtime_dependency in runtime_dependencies:
            for name, files in runtime_dependency.items():
                for dependency in conan_dependencies:
                    if dependency.ref.name == name:
                        result[package][name] = files(dependency)
                        break
    return result

def generate_package(package, conan_dependencies, output_folder):
    deploy_dir = os.path.join(output_folder, "packages", package[0])
    package_name = os.path.join(output_folder, "packages", package[0] + package[1])
    os.makedirs(deploy_dir, exist_ok=True)
    for name, dependencies in package[2].items():
        package_folder = None
        for dependency in conan_dependencies:
            if dependency.ref.name == name:
                if dependency.package_folder:
                    package_folder = dependency.package_folder
                break
        if not package_folder:
            continue
        for dependency in dependencies:
            src = os.path.join(package_folder, dependency[1], '')
            dst = os.path.join(deploy_dir, dependency[2], '')
            if os.path.exists(src):
                for file in glob.glob(os.path.join(src, dependency[0])):
                    if not os.path.exists(dst):
                        os.makedirs(dst, exist_ok = True)
                    shutil.copy(file, dst)
    # copy terminfo db into the package
    terminfo = None
    for dependency in conan_dependencies:
        if dependency.ref.name == "ncurses":
            terminfo = dependency.runenv_info.vars(dependency, scope="run").get("TERMINFO", None)
            break
    if terminfo and os.path.exists(terminfo):
        datadir = os.path.join(deploy_dir, "share")
        os.makedirs(datadir, exist_ok = True)
        if os.path.isdir(terminfo):
            dst = os.path.join(datadir, os.path.basename(os.path.normpath(terminfo)))
            shutil.copytree(terminfo, dst, dirs_exist_ok = True)
        else:
            shutil.copy(terminfo, datadir)
    if package[1] == ".zip":
        with zipfile.ZipFile(package_name, "w", zipfile.ZIP_DEFLATED) as zipf:
            for root, _, files in os.walk(deploy_dir):
                for file in files:
                    file_path = os.path.join(root, file)
                    arcname = os.path.relpath(file_path, deploy_dir)
                    zipf.write(file_path, arcname)
    elif package[1] == ".tar.gz":
        def reset(tarinfo):
            tarinfo.uid = tarinfo.gid = 0
            tarinfo.uname = tarinfo.gname = "root"
            return tarinfo
        with tarfile.open(package_name, "w:gz") as tar:
            tar.add(deploy_dir, arcname = ".", filter = reset)
            tar.close()
    else:
        raise Exception("unsupported package extension '" + package[1] + "'")
    shutil.rmtree(deploy_dir)

def deploy(graph, output_folder):
    conan_dependencies = graph.root.conanfile.dependencies.values()
    package_name = os.environ.get("EXPORT_PACKAGE_NAME", "rstream-utils")
    packages = get_packages(graph, conan_dependencies)
    if package_name in packages:
        runtime_dependencies = packages[package_name]
        graph.root.conanfile.output.info("generating package '" + package_name + "'...")
        extension = ".zip" if graph.root.conanfile.settings.os == "Windows" else ".tar.gz"
        generate_package([package_name, extension, runtime_dependencies], conan_dependencies, output_folder)
    else:
        raise Exception("package '" + package_name + "' not found")
