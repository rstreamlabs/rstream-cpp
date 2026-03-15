#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# See LICENSE file in the project root for license information.

"""
embed-default-ca-certificates

usage:
  embed-default-ca-certificates [options]
  embed-default-ca-certificates (-h|--help)

options:
  -h --help         show this screen
  --cpp-out=ARG     c++ file to be generated [default: default_ca_certificates.cpp]
  --url=ARG         url to download the certificate [default: https://curl.se/ca/cacert.pem]
"""

import importlib
import os
import site
import subprocess
import sys

def import_or_install(package):
    try:
        module = __import__(package)
    except ImportError:
        env = os.environ.copy()
        env['PIP_BREAK_SYSTEM_PACKAGES'] = '1'
        if subprocess.call([sys.executable, "-m", "pip", "install", package], env=env) != 0:
            print(f"Failed to install {package}. Please install it manually.")
            sys.exit(1)
        importlib.reload(site)
        module = __import__(package)
    return module

docopt = import_or_install("docopt")
requests = import_or_install("requests")

def download_certificate(url):
    response = requests.get(url)
    response.raise_for_status()
    return response.text

def escape_string(s):
    return s.replace("\"", "\\\"").replace("\n", "\\n")

def write_cpp_file(file, cert_data):
    file.write("// See LICENSE file in the project root for license information.\n\n")
    file.write("#include <string>\n\n")
    file.write("namespace rstream { namespace io {\n\n")
    file.write(f"static const std::string g_default_ca_certificates = \"{escape_string(cert_data)}\";")
    file.write("\n\n")
    file.write("const std::string& get_default_ca_certificates() {\n")
    file.write("\treturn g_default_ca_certificates;\n")
    file.write("}\n\n")
    file.write("} }\n")

def main():
    args = docopt.docopt(__doc__)
    cert_data = download_certificate(args["--url"])
    out = args["--cpp-out"]
    with open(out, 'w+') as file:
        write_cpp_file(file, cert_data)

if __name__ == "__main__":
    main()
