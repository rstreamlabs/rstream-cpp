#!/usr/bin/env python3

import importlib.util
import os
import unittest


REPOSITORY_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT_PATH = os.path.join(REPOSITORY_ROOT, "conan", "check_public_dependencies.py")
SPEC = importlib.util.spec_from_file_location("check_public_dependencies", SCRIPT_PATH)
CHECK_PUBLIC_DEPENDENCIES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK_PUBLIC_DEPENDENCIES)


def graph_with_dependency(**dependency):
    return {
        "graph": {
            "nodes": {
                "0": {
                    "id": "0",
                    "ref": "rstream/1.13.0",
                    "url": "https://github.com/rstreamlabs/rstream-cpp",
                },
                "1": {
                    "id": "1",
                    "ref": "boost/1.91.0",
                    "url": CHECK_PUBLIC_DEPENDENCIES.CONAN_CENTER_URL,
                    **dependency,
                },
            }
        }
    }


class ConanPublicDependenciesTest(unittest.TestCase):
    def test_accepts_conan_center_dependency(self):
        self.assertEqual(
            CHECK_PUBLIC_DEPENDENCIES.private_dependencies(graph_with_dependency()),
            [],
        )

    def test_rejects_user_channel_reference(self):
        violations = CHECK_PUBLIC_DEPENDENCIES.private_dependencies(
            graph_with_dependency(
                ref="boost/1.91.0@conan/stable",
                user="conan",
                channel="stable",
            )
        )
        self.assertIn(
            "boost/1.91.0@conan/stable: user/channel references are not public",
            violations,
        )

    def test_rejects_non_conan_center_recipe(self):
        violations = CHECK_PUBLIC_DEPENDENCIES.private_dependencies(
            graph_with_dependency(url="https://packages.example.com/boost")
        )
        self.assertIn(
            "boost/1.91.0: recipe source is https://packages.example.com/boost",
            violations,
        )

    def test_rejects_missing_recipe_origin(self):
        violations = CHECK_PUBLIC_DEPENDENCIES.private_dependencies(
            graph_with_dependency(url=None)
        )
        self.assertIn(
            "boost/1.91.0: recipe source is missing recipe URL",
            violations,
        )


if __name__ == "__main__":
    unittest.main()
