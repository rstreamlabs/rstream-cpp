#!/usr/bin/env bash

set -euo pipefail

if [[ $# -lt 3 ]]; then
  echo "usage: $0 <destination> <source-sha> <package-pattern>..." >&2
  exit 1
fi

destination=$1
source_sha=$2
shift 2

if [[ -e "$destination" ]]; then
  echo "candidate destination already exists: ${destination}" >&2
  exit 1
fi

temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT
mkdir -p "$destination"

lists=()
index=0
for pattern in "$@"; do
  list="$temporary/packages-${index}.json"
  conan list "$pattern" --format=json > "$list"
  if jq --exit-status '[.[] | keys[]] | length > 0' "$list" >/dev/null; then
    lists+=(--list "$list")
  fi
  index=$((index + 1))
done

if [[ ${#lists[@]} -eq 0 ]]; then
  echo "no Conan packages matched the candidate patterns" >&2
  exit 1
fi

conan pkglist merge "${lists[@]}" --format=json > "$destination/packages.json"
conan cache check-integrity --list "$destination/packages.json"
conan cache save --list "$destination/packages.json" --file "$destination/cache.tgz"
jq -n --arg source "$source_sha" '{schema: 1, source: $source}' > "$destination/manifest.json"

(
  cd "$destination"
  shasum -a 256 cache.tgz packages.json manifest.json > SHA256SUMS
  shasum -a 256 -c SHA256SUMS
)
