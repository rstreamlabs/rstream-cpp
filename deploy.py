#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# See LICENSE file in the project root for license information.

import base64
import glob
import os
import shutil
import subprocess
import tarfile
import tempfile
import zipfile

"""Conan deployer for rstream packages.
"""
def require_env(names):
    missing = [ name for name in names if not os.environ.get(name) ]
    if missing:
        raise Exception("missing required environment variables: " + ", ".join(missing))

def write_base64_secret(temp_dir, env_name, filename):
    value = os.environ.get(env_name)
    if not value:
        raise Exception("missing required environment variable: " + env_name)
    path = os.path.join(temp_dir, filename)
    with open(path, "wb") as fp:
        fp.write(base64.b64decode(value.encode("utf-8")))
    os.chmod(path, 0o600)
    return path

def package_file_requires_macos_signature(file_path):
    filename = os.path.basename(file_path)
    return os.access(file_path, os.X_OK) or filename.endswith(".dylib") or ".dylib." in filename or filename.endswith(".so") or ".so." in filename

def sign_macos_file_with_rcodesign(file_path, mode, temp_dir):
    if mode == "adhoc":
        subprocess.run(["rcodesign", "sign", file_path], check = True)
        return
    require_env(["MACOS_CERTIFICATE_PWD"])
    certificate_file = os.environ.get("MACOS_CERTIFICATE_FILE") or write_base64_secret(temp_dir, "MACOS_CERTIFICATE", "certificate.p12")
    subprocess.run(["rcodesign", "sign", "--p12-file", certificate_file, "--p12-password", os.environ["MACOS_CERTIFICATE_PWD"], "--code-signature-flags", "runtime", file_path], check = True)

def sign_macos_file_with_codesign(file_path, mode, temp_dir):
    identifier = os.environ.get("MACOS_CODESIGN_IDENTIFIER", "io.rstream")
    if mode == "adhoc":
        subprocess.run(["codesign", "-f", "-i", identifier, "-s", "-", "-v", file_path], check = True)
        return
    require_env(["MACOS_CERTIFICATE_NAME", "MACOS_NOTARIZATION_APPLE_ID", "MACOS_NOTARIZATION_TEAM_ID", "MACOS_NOTARIZATION_PWD"])
    subprocess.run(["codesign", "--options=runtime", "--timestamp", "-f", "-i", identifier, "-s", os.environ["MACOS_CERTIFICATE_NAME"], "-v", file_path], check = True)

def create_macos_notarization_archive(deploy_dir, temp_dir):
    archive_path = os.path.join(temp_dir, "payload.zip")
    with zipfile.ZipFile(archive_path, "w", zipfile.ZIP_DEFLATED) as zipf:
        for root, _, filenames in os.walk(deploy_dir):
            for filename in filenames:
                file_path = os.path.join(root, filename)
                arcname = os.path.relpath(file_path, deploy_dir)
                zipf.write(file_path, arcname)
    return archive_path

def notarize_macos_payload_with_rcodesign(deploy_dir, temp_dir):
    api_key_file = os.environ.get("MACOS_APP_STORE_API_KEY_FILE") or write_base64_secret(temp_dir, "MACOS_APP_STORE_API_KEY", "app-store-api-key.json")
    archive_path = create_macos_notarization_archive(deploy_dir, temp_dir)
    subprocess.run(["rcodesign", "notary-submit", "-v", "--api-key-file", api_key_file, "--wait", archive_path], check = True)

def notarize_macos_payload_with_codesign(deploy_dir, temp_dir):
    require_env(["MACOS_NOTARIZATION_APPLE_ID", "MACOS_NOTARIZATION_TEAM_ID", "MACOS_NOTARIZATION_PWD"])
    archive_path = create_macos_notarization_archive(deploy_dir, temp_dir)
    subprocess.run(["xcrun", "notarytool", "submit", "--apple-id", os.environ["MACOS_NOTARIZATION_APPLE_ID"], "--team-id", os.environ["MACOS_NOTARIZATION_TEAM_ID"], "--password", os.environ["MACOS_NOTARIZATION_PWD"], "--wait", archive_path], check = True)

def sign_macos_payload(conanfile, deploy_dir):
    if str(conanfile.settings.os) != "Macos":
        return
    mode = os.environ.get("MACOS_CODESIGN_MODE", "").strip()
    if not mode:
        return
    if mode not in ("adhoc", "certificate"):
        raise Exception("MACOS_CODESIGN_MODE must be one of: adhoc, certificate")
    tool = os.environ.get("MACOS_CODESIGN_TOOL", "rcodesign").strip()
    if tool not in ("rcodesign", "codesign"):
        raise Exception("MACOS_CODESIGN_TOOL must be one of: rcodesign, codesign")
    files = []
    for root, _, filenames in os.walk(deploy_dir):
        for filename in filenames:
            file_path = os.path.join(root, filename)
            if package_file_requires_macos_signature(file_path):
                files.append(file_path)
    if not files:
        conanfile.output.warning("macOS code signing enabled but no signable files were found")
        return
    with tempfile.TemporaryDirectory() as temp_dir:
        for file_path in sorted(files):
            conanfile.output.info("signing '" + os.path.relpath(file_path, deploy_dir) + "' with " + tool + "...")
            if tool == "rcodesign":
                sign_macos_file_with_rcodesign(file_path, mode, temp_dir)
            else:
                sign_macos_file_with_codesign(file_path, mode, temp_dir)
        if mode == "certificate":
            conanfile.output.info("submitting macOS payload for notarization with " + tool + "...")
            if tool == "rcodesign":
                notarize_macos_payload_with_rcodesign(deploy_dir, temp_dir)
            else:
                notarize_macos_payload_with_codesign(deploy_dir, temp_dir)

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

def generate_package(conanfile, package, conan_dependencies, output_folder):
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
    sign_macos_payload(conanfile, deploy_dir)
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
        generate_package(graph.root.conanfile, [package_name, extension, runtime_dependencies], conan_dependencies, output_folder)
    else:
        raise Exception("package '" + package_name + "' not found")
