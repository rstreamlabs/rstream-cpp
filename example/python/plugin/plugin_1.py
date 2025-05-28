#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# See LICENSE file in the project root for license information.

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../../binding/python/lib/python/src"))

from rstream import Element

from interface import Interface

def get_elements():
    return [ element_1 ]

class element_1(Interface, Element):
    def get_info():
        return Element.Info("sample element #1", "sample element #1")
    def run(self):
        return 1
