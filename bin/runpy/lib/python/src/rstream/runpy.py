#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# See LICENSE file in the project root for license information.

import os
import re
import runpy
import sys

def run():
    pattern = '^rstream(-[a-z]+)+$'
    exec = os.path.basename(sys.argv[0])
    if re.match(pattern, exec):
        exec = exec.replace('-', '.')
        try:
            runpy._run_module_as_main(exec)
        except SystemExit as e:
            if e.code:
                if isinstance(e.code, str):
                    print(e.code)
                if isinstance(e.code, int):
                    return e.code
                else:
                    return 1
        return 0
    else:
        sys.stderr.write("invalid executable name '" + exec + "'\n")
        return 1
