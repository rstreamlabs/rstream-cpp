#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "usage: $0 <candidate> <source-sha> <remote>" >&2
  exit 1
fi

candidate=$1
source_sha=$2
remote=$3

if [[ ! -f "$candidate/manifest.json" || "$(jq -r '.source' "$candidate/manifest.json")" != "$source_sha" ]]; then
  echo "Conan candidate does not match the approved commit" >&2
  exit 1
fi

(
  cd "$candidate"
  shasum -a 256 -c SHA256SUMS
)

conan cache restore "$candidate/cache.tgz"
conan cache check-integrity --list "$candidate/packages.json"
conan upload --list "$candidate/packages.json" -r "$remote" --check --confirm
