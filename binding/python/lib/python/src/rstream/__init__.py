#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# See LICENSE file in the project root for license information.

import os
import sys

deps = os.path.join(os.path.dirname(os.path.realpath(__file__)), "deps")
if os.path.exists(deps):
    sys.path.insert(0, deps)

from rstream.plugin.element import Element
