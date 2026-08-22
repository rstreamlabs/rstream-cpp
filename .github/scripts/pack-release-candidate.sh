#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 7 ]]; then
  echo "usage: $0 <destination> <source-sha> <version> <channel> <os> <arch> <package-name>" >&2
  exit 1
fi

destination=$1
source_sha=$2
version=$3
channel=$4
os=$5
arch=$6
package_name=$7

if [[ -e "$destination" ]]; then
  echo "candidate destination already exists: ${destination}" >&2
  exit 1
fi

mkdir -p "$destination/package-api"
cp -R out/release/. "$destination/package-api/"
jq -n \
  --arg source "$source_sha" \
  --arg version "$version" \
  --arg channel "$channel" \
  --arg os "$os" \
  --arg arch "$arch" \
  --arg package_name "$package_name" \
  '{schema: 1, source: $source, version: $version, channel: $channel, os: $os, arch: $arch, packageName: $package_name}' \
  > "$destination/release.json"

python3 - "$destination" <<'PY'
from hashlib import sha256
from pathlib import Path
import sys

root = Path(sys.argv[1])
with (root / "SHA256SUMS").open("w", encoding="utf-8") as manifest:
    files = (
        item
        for item in root.rglob("*")
        if item.is_file() and item.name != "SHA256SUMS"
    )
    for path in sorted(files):
        relative = path.relative_to(root).as_posix()
        manifest.write(f"{sha256(path.read_bytes()).hexdigest()}  {relative}\n")
PY

(
  cd "$destination"
  shasum -a 256 -c SHA256SUMS
)
