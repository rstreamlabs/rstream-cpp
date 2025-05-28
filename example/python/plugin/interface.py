#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# See LICENSE file in the project root for license information.

from abc import ABC, abstractmethod

class Interface(ABC):
    @abstractmethod
    def run(self): raise NotImplementedError
