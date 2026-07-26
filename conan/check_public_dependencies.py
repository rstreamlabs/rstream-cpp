#!/usr/bin/env python3

import argparse
import json
import sys


CONAN_CENTER_URL = "https://github.com/conan-io/conan-center-index"


def private_dependencies(graph):
    violations = []
    nodes = graph.get("graph", {}).get("nodes", {})
    for node in nodes.values():
        if node.get("id") == "0" or not node.get("ref"):
            continue
        reference = node["ref"]
        if node.get("user") or node.get("channel"):
            violations.append(f"{reference}: user/channel references are not public")
        if node.get("url") != CONAN_CENTER_URL:
            source = node.get("url") or "missing recipe URL"
            violations.append(f"{reference}: recipe source is {source}")
    return violations


def main():
    parser = argparse.ArgumentParser(
        description="Verify that a Conan graph only uses public Conan Center recipes."
    )
    parser.add_argument("graph", help="Path to `conan graph info --format=json` output.")
    args = parser.parse_args()
    with open(args.graph, encoding="utf-8") as graph_file:
        graph = json.load(graph_file)
    violations = private_dependencies(graph)
    if violations:
        for violation in violations:
            print(violation, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
