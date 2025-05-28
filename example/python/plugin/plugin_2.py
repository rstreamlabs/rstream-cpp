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
    return [ element_2 ]

class element_2(Interface, Element):
    def get_info():
        return Element.Info("sample element #2", "sample element #2")
    def run(self):
        return 2
