#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 6 ]]; then
  echo "usage: $0 <candidate> <source-sha> <version> <channel> <os> <arch>" >&2
  exit 1
fi

candidate=$1
source_sha=$2
version=$3
channel=$4
os=$5
arch=$6

(
  cd "$candidate"
  shasum -a 256 -c SHA256SUMS
)

jq --exit-status \
  --arg source "$source_sha" \
  --arg version "$version" \
  --arg channel "$channel" \
  --arg os "$os" \
  --arg arch "$arch" \
  '.source == $source and .version == $version and .channel == $channel and .os == $os and .arch == $arch' \
  "$candidate/release.json" >/dev/null
