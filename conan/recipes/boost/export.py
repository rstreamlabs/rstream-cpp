#!/usr/bin/python3
import subprocess

import yaml

with open("config.yml", encoding="utf-8") as config_file:
    config = yaml.safe_load(config_file)

for version, metadata in config["versions"].items():
    subprocess.run([
        "conan",
        "export",
        "--user",
        "conan",
        "--channel",
        "stable",
        "--version",
        version,
        metadata["folder"],
    ], check=True)
