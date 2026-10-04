import ast
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


class ConanSourceExportsTest(unittest.TestCase):
    def test_exports_project_sources_without_local_build_caches(self):
        root = Path(__file__).resolve().parents[1]
        recipe = ast.parse((root / "conanfile.py").read_text(encoding="utf-8"))
        patterns = next(
            ast.literal_eval(node.value)
            for node in ast.walk(recipe)
            if isinstance(node, ast.Assign)
            and any(isinstance(target, ast.Name) and target.id == "exports_sources"
                    for target in node.targets)
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            source = path / "source"
            source.mkdir()
            (source / "conanfile.py").write_text(
                "from conan import ConanFile\n"
                "class Probe(ConanFile):\n"
                "    name = 'rstream-export-probe'\n"
                "    version = '0.0.0'\n"
                f"    exports_sources = {patterns!r}\n",
                encoding="utf-8",
            )
            for filename in ("lib/source.cpp", ".ccache/cached-object",
                             ".conan2/p/cached-package.cpp"):
                marker = source / filename
                marker.parent.mkdir(parents=True, exist_ok=True)
                marker.write_text("source export probe\n", encoding="utf-8")
            environment = dict(os.environ, CONAN_HOME=str(path / "cache"))

            def conan(*args):
                return subprocess.run(
                    [sys.executable, "-c",
                     "import sys; from conan.cli.cli import main; main(sys.argv[1:])",
                     *args], env=environment, check=True, capture_output=True,
                    text=True, timeout=30,
                ).stdout

            conan("export", str(source))
            exported = Path(conan("cache", "path", "rstream-export-probe/0.0.0",
                                  "--folder=export_source").strip())
            self.assertEqual((exported / "lib/source.cpp").read_text(encoding="utf-8"),
                             "source export probe\n")
            self.assertFalse((exported / ".ccache").exists())
            self.assertFalse((exported / ".conan2").exists())


if __name__ == "__main__":
    unittest.main()
