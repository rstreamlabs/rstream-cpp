import json
import os
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path


class ReleaseWorkflowTest(unittest.TestCase):
    def setUp(self) -> None:
        self.workflow = Path(".github/workflows/release-packages.yml").read_text(
            encoding="utf-8"
        )
        self.publication = Path(".github/workflows/publish-packages.yml").read_text(
            encoding="utf-8"
        )
        self.config = json.loads(
            Path("release-please-config.json").read_text(encoding="utf-8")
        )

    def test_publication_uses_approved_candidates(self) -> None:
        candidate, dispatch = self.workflow.split("  publish:\n", maxsplit=1)
        publication = self.publication
        self.assertIn("uses: ./.github/workflows/publish-packages.yml", dispatch)

        self.assertNotIn("conan upload", candidate)
        self.assertNotIn("RSTREAM_TOKEN", candidate)
        self.assertNotIn("./build-conan-cross.sh upload", candidate)
        self.assertIn("environment: stable-release", publication)
        self.assertIn("actions/download-artifact@", publication)
        self.assertIn("run: ./.github/scripts/verify-release-approval.sh", publication)
        self.assertIn("run-id: ${{ inputs.candidate_run_id }}", publication)
        self.assertIn("publish-conan-candidate.sh", publication)
        self.assertIn("verify-release-candidate.sh", publication)

    def test_github_release_stays_draft_until_publication(self) -> None:
        self.assertTrue(self.config["draft"])
        self.assertTrue(self.config["force-tag-creation"])
        self.assertIn(
            'gh release edit "${RELEASE_TAG}" --draft=false', self.publication
        )

    def test_every_release_job_installs_pinned_recipe_dependencies(self) -> None:
        self.assertIn('PYYAML_VERSION: "6.0.3"', self.workflow)
        self.assertIn('PYYAML_VERSION: "6.0.3"', self.publication)
        self.assertEqual(
            (self.workflow + self.publication).count(
                'python3 -m pip install --user "PyYAML==${PYYAML_VERSION}"'
            ),
            4,
        )


class PackageUploadTest(unittest.TestCase):
    def run_upload(self, platform: str, failure: str = "") -> tuple:
        repository = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            shutil.copyfile(
                repository / "build-conan-cross.sh", root / "build-conan-cross.sh"
            )
            tools = root / "bin"
            tools.mkdir()
            commands = {
                "conan": (
                    "print('Conan version 2.31.2' if sys.argv[1] == '--version' "
                    "else '{\"name\":\"rstream\",\"version\":\"1.15.1\"}')"
                ),
                "uname": f'print({("Darwin" if platform == "macos" else "Linux")!r})',
                "jq": 'import urllib.parse; print(urllib.parse.quote(sys.stdin.read(), safe=""))',
                "curl": textwrap.dedent('''
                    from pathlib import Path
                    phase = "transfer" if "--upload-file" in sys.argv else "metadata"
                    with open("requests.log", "a", encoding="utf-8") as log:
                        log.write(phase + "\\n")
                    if os.environ["UPLOAD_FAILURE"] == phase:
                        print("simulated curl failure", file=sys.stderr)
                        sys.exit(35 if phase == "transfer" else 22)
                    if phase == "metadata":
                        print("HTTP/2 307\\nx-package-id: fixture-package\\nlocation: https://storage.example.invalid/archive\\n")
                    else:
                        archive = Path(sys.argv[sys.argv.index("--upload-file") + 1])
                        Path("uploaded.bin").write_bytes(archive.read_bytes())
                '''),
            }
            for name, source in commands.items():
                command = tools / name
                command.write_text(
                    f"#!{sys.executable}\nimport os, sys\n" + source + "\n",
                    encoding="utf-8",
                )
                command.chmod(0o755)
            target = root / "out/release/1.15.1" / platform / "x86_64"
            if platform == "linux":
                target /= "musl"
            target /= "static/packages"
            target.mkdir(parents=True)
            archive = target / (
                "rstream-utils.zip" if platform == "windows" else "rstream-utils.tar.gz"
            )
            archive.write_bytes(b"retained release archive")
            env = dict(
                os.environ,
                PATH=str(tools) + os.pathsep + os.environ["PATH"],
                OSS=platform,
                USE_DOCKER="off",
                USE_PATCHED_CONAN_DEPS="off",
                CHANNEL="stable",
                RSTREAM_URL="https://api.example.invalid",
                RSTREAM_TOKEN="fixture-token",
                UPLOAD_FAILURE=failure,
            )
            env.update({
                platform.upper() + "_ARCHS": "x86_64",
                platform.upper() + "_BUILD_SHARED": "off",
                "LINUX_TCLIBCS": "musl",
            })
            result = subprocess.run(
                [shutil.which("bash"), "build-conan-cross.sh", "upload"],
                cwd=root,
                env=env,
                capture_output=True,
                text=True,
                timeout=30,
            )
            receipt = Path(str(archive) + ".info")
            uploaded = root / "uploaded.bin"
            return (
                result,
                receipt.read_text() if receipt.exists() else None,
                (root / "requests.log").read_text().splitlines(),
                uploaded.read_bytes() if uploaded.exists() else None,
            )

    def test_upload_preserves_the_archive_and_records_success(self) -> None:
        for platform in ("linux", "macos", "windows"):
            with self.subTest(platform=platform):
                result, receipt, requests, uploaded = self.run_upload(platform)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("id:fixture-package", receipt)
                self.assertEqual(requests, ["metadata", "transfer"])
                self.assertEqual(uploaded, b"retained release archive")

    def test_metadata_failure_stops_before_transfer(self) -> None:
        for platform in ("linux", "macos", "windows"):
            with self.subTest(platform=platform):
                result, receipt, requests, _ = self.run_upload(platform, "metadata")
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIsNone(receipt)
                self.assertEqual(requests, ["metadata"])
                self.assertNotIn("Package uploaded:", result.stdout)

    def test_transfer_failure_does_not_record_success(self) -> None:
        for platform in ("linux", "macos", "windows"):
            with self.subTest(platform=platform):
                result, receipt, requests, uploaded = self.run_upload(platform, "transfer")
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIsNone(receipt)
                self.assertIsNone(uploaded)
                self.assertEqual(requests, ["metadata", "transfer"])
                self.assertNotIn("Package uploaded:", result.stdout)


if __name__ == "__main__":
    unittest.main()
