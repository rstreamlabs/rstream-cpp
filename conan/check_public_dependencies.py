#!/usr/bin/env python3

import argparse
import json
import subprocess
import sys


CONAN_CENTER_URL = "https://github.com/conan-io/conan-center-index"
CONAN_CENTER_REMOTE_URL = "https://center2.conan.io"


def private_dependencies(graph):
    violations = []
    nodes = graph.get("graph", {}).get("nodes", {})
    for node in nodes.values():
        if node.get("id") == "0" or not node.get("ref"):
            continue
        reference = node["ref"]
        if node.get("user") or node.get("channel") or "@" in reference:
            violations.append(f"{reference}: user/channel references are not public")
        if node.get("url") != CONAN_CENTER_URL:
            source = node.get("url") or "missing recipe URL"
            violations.append(f"{reference}: recipe source is {source}")
    return violations


def verify_public_recipes(graph, conan="conan"):
    """Check public revision membership and local recipe/package integrity.

    Recipe URL metadata alone also matches a locally edited Conan Center recipe.
    Conan's revision and manifest checks make that case observable without
    maintaining a separate list of trusted recipe hashes in this repository.
    """
    def command(*args):
        return subprocess.run(
            [conan, *args], check=True, capture_output=True, text=True
        ).stdout

    remotes = json.loads(command("remote", "list", "--format=json"))
    remote = next((item for item in remotes if item["name"] == "conancenter"), None)
    if (not remote or remote.get("url") != CONAN_CENTER_REMOTE_URL
            or not remote.get("verify_ssl") or not remote.get("enabled")):
        raise ValueError("conancenter must be enabled with TLS verification at "
                         + CONAN_CENTER_REMOTE_URL)
    references = set()
    for node in graph.get("graph", {}).get("nodes", {}).values():
        if node.get("id") == "0" or not node.get("ref"):
            continue
        reference = node["ref"]
        if "#" not in reference or not reference.split("#", 1)[1]:
            raise ValueError(f"{reference}: recipe revision is missing")
        references.add(reference)
    for reference in sorted(references):
        name, revision = reference.split("#", 1)
        result = json.loads(command("list", reference, "-r=conancenter", "--format=json"))
        revisions = result.get("conancenter", {}).get(name, {}).get("revisions", {})
        if revision not in revisions:
            raise ValueError(f"{reference}: recipe revision is not on Conan Center")
        command("cache", "check-integrity", reference)
        print(f"Verified public recipe and local integrity: {reference}")


def main():
    parser = argparse.ArgumentParser(
        description="Verify that a Conan graph only uses public Conan Center recipes."
    )
    parser.add_argument("graph", help="Path to `conan graph info --format=json` output.")
    parser.add_argument("--verify-recipes", action="store_true",
                        help="Verify revisions against Conan Center and check local cache integrity.")
    parser.add_argument("--conan", default="conan", help="Conan executable for recipe verification.")
    args = parser.parse_args()
    with open(args.graph, encoding="utf-8") as graph_file:
        graph = json.load(graph_file)
    violations = private_dependencies(graph)
    if violations:
        for violation in violations:
            print(violation, file=sys.stderr)
        return 1
    if args.verify_recipes:
        try:
            verify_public_recipes(graph, args.conan)
        except (ValueError, OSError, subprocess.CalledProcessError) as error:
            print(f"Public recipe verification failed: {error}", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
