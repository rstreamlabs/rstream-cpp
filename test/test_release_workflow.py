import json
import unittest
from pathlib import Path


class ReleaseWorkflowTest(unittest.TestCase):
    def setUp(self) -> None:
        self.workflow = Path(".github/workflows/release-packages.yml").read_text(
            encoding="utf-8"
        )
        self.config = json.loads(
            Path("release-please-config.json").read_text(encoding="utf-8")
        )

    def test_publication_uses_approved_candidates(self) -> None:
        candidate, publication = self.workflow.split("  approve:\n", maxsplit=1)

        self.assertNotIn("conan upload", candidate)
        self.assertNotIn("RSTREAM_TOKEN", candidate)
        self.assertNotIn("./build-conan-cross.sh upload", candidate)
        self.assertIn("environment: stable-release", publication)
        self.assertIn("actions/download-artifact@", publication)
        self.assertIn("publish-conan-candidate.sh", publication)
        self.assertIn("verify-release-candidate.sh", publication)

    def test_github_release_stays_draft_until_publication(self) -> None:
        self.assertTrue(self.config["draft"])
        self.assertTrue(self.config["force-tag-creation"])
        self.assertIn(
            'gh release edit "${GITHUB_REF_NAME}" --draft=false', self.workflow
        )


if __name__ == "__main__":
    unittest.main()
