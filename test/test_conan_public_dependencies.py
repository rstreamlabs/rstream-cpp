#!/usr/bin/env python3

import importlib.util
import json
import os
import subprocess
import unittest
from unittest.mock import Mock, patch


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

    def test_rejects_private_reference_even_without_user_metadata(self):
        violations = CHECK_PUBLIC_DEPENDENCIES.private_dependencies(
            graph_with_dependency(ref="boost/1.91.0@private/stable")
        )
        self.assertTrue(violations)

    def verification_responses(self, revisions=None):
        return [
            Mock(stdout=json.dumps([{
                "name": "conancenter", "url": CHECK_PUBLIC_DEPENDENCIES.CONAN_CENTER_REMOTE_URL,
                "verify_ssl": True, "enabled": True,
            }])),
            Mock(stdout=json.dumps({"conancenter": {"boost/1.91.0": {
                "revisions": {"public-revision": {}} if revisions is None else revisions,
            }}})),
            Mock(stdout=""),
        ]

    def test_verifies_revision_membership_and_cache_integrity(self):
        graph = graph_with_dependency(ref="boost/1.91.0#public-revision")
        with patch.object(subprocess, "run", side_effect=self.verification_responses()) as run:
            CHECK_PUBLIC_DEPENDENCIES.verify_public_recipes(graph)
        self.assertEqual(run.call_args_list[-1].args[0],
                         ["conan", "cache", "check-integrity", "boost/1.91.0#public-revision"])

    def test_rejects_local_revision_with_unchanged_public_url(self):
        graph = graph_with_dependency(ref="boost/1.91.0#local-revision")
        with patch.object(subprocess, "run", side_effect=self.verification_responses()):
            with self.assertRaisesRegex(ValueError, "not on Conan Center"):
                CHECK_PUBLIC_DEPENDENCIES.verify_public_recipes(graph)

    def test_rejects_corrupt_local_recipe(self):
        responses = self.verification_responses()
        responses[-1] = subprocess.CalledProcessError(1, ["conan", "cache", "check-integrity"])
        graph = graph_with_dependency(ref="boost/1.91.0#public-revision")
        with patch.object(subprocess, "run", side_effect=responses):
            with self.assertRaises(subprocess.CalledProcessError):
                CHECK_PUBLIC_DEPENDENCIES.verify_public_recipes(graph)

    def test_rejects_remote_name_pointing_to_private_server(self):
        response = Mock(stdout=json.dumps([{
            "name": "conancenter", "url": "https://packages.example.com",
            "verify_ssl": True, "enabled": True,
        }]))
        with patch.object(subprocess, "run", return_value=response):
            with self.assertRaisesRegex(ValueError, "TLS verification"):
                CHECK_PUBLIC_DEPENDENCIES.verify_public_recipes(graph_with_dependency())


if __name__ == "__main__":
    unittest.main()
