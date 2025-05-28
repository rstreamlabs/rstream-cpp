#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# See LICENSE file in the project root for license information.

from abc import ABC, abstractmethod

class Element(ABC):
    class Info:
        def __init__(self, name, description):
            self.name = name
            self.description = description
        def get(self):
            return (self.name, self.description)
    @staticmethod
    @abstractmethod
    def get_info(): raise NotImplementedError
