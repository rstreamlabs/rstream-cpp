#!/usr/bin/python3
import yaml, os
f = open('config.yml')
yaml = yaml.safe_load(f)
for version in yaml["versions"]:
    os.system("conan export --user conan --channel stable --version " + version + " " + yaml["versions"][version]["folder"])
