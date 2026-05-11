#!/usr/bin/env bash
# See LICENSE file in the project root for license information.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD_DIR="${RSTREAM_CPP_COVERAGE_BUILD_DIR:-$ROOT/build/codex-coverage}"
PROFILE_DIR="$BUILD_DIR/profiles/current"
LLVM_COV="${LLVM_COV:-llvm-cov}"
LLVM_PROFDATA="${LLVM_PROFDATA:-llvm-profdata}"
IGNORE_REGEX='external|generated-cpp-sources|build/codex-coverage|(^|/)test/|(^|/)demo/'
PRODUCT_OBJECT_ROOTS=(
  "$BUILD_DIR/lib"
  "$BUILD_DIR/plugin"
  "$BUILD_DIR/bin/tunnel/lib"
  "$BUILD_DIR/bin/rtty/lib"
  "$BUILD_DIR/bin/ncat/lib"
  "$BUILD_DIR/bin/nperf/lib/cpp"
)
PRODUCT_SOURCE_ROOTS=(
  "$ROOT/lib"
  "$ROOT/plugin"
  "$ROOT/bin/tunnel/lib"
  "$ROOT/bin/rtty/lib"
  "$ROOT/bin/ncat/lib"
  "$ROOT/bin/nperf/lib/cpp"
)

if ! command -v "$LLVM_COV" >/dev/null 2>&1; then
  if [ -x /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/llvm-cov ]; then
    LLVM_COV=/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/llvm-cov
  else
    printf "ERROR llvm-cov not found; set LLVM_COV\n" >&2
    exit 2
  fi
fi

if ! command -v "$LLVM_PROFDATA" >/dev/null 2>&1; then
  if [ -x /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/llvm-profdata ]; then
    LLVM_PROFDATA=/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/llvm-profdata
  else
    printf "ERROR llvm-profdata not found; set LLVM_PROFDATA\n" >&2
    exit 2
  fi
fi

if ! command -v jq >/dev/null 2>&1; then
  printf "ERROR jq not found\n" >&2
  exit 2
fi

cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DENABLE_TESTING=ON \
  -DCMAKE_C_FLAGS="-fprofile-instr-generate -fcoverage-mapping" \
  -DCMAKE_CXX_FLAGS="-fprofile-instr-generate -fcoverage-mapping"

cmake --build "$BUILD_DIR" --target clean
rm -rf "$PROFILE_DIR"
mkdir -p "$PROFILE_DIR"

LLVM_PROFILE_FILE="$PROFILE_DIR/%p-%m.profraw" \
  cmake --build "$BUILD_DIR" --target check -j

"$LLVM_PROFDATA" merge -sparse "$PROFILE_DIR"/*.profraw -o "$PROFILE_DIR/coverage.profdata"

search_roots=()
for path in "${PRODUCT_OBJECT_ROOTS[@]}"; do
  if [ -d "$path" ]; then
    search_roots+=("$path")
  fi
done
if [ "${#search_roots[@]}" -eq 0 ]; then
  printf "ERROR no product object search roots found under %s\n" "$BUILD_DIR" >&2
  exit 2
fi

objects=()
while IFS= read -r object; do
  objects+=("$object")
done < <(find "${search_roots[@]}" -type f -name '*.o' | sort)

if [ "${#objects[@]}" -eq 0 ]; then
  printf "ERROR no product object files found under %s\n" "$BUILD_DIR" >&2
  exit 2
fi

object_args=()
for object in "${objects[@]:1}"; do
  object_args+=("-object=$object")
done

"$LLVM_COV" export "${objects[0]}" "${object_args[@]}" \
  -instr-profile="$PROFILE_DIR/coverage.profdata" \
  -ignore-filename-regex="$IGNORE_REGEX" \
  > "$PROFILE_DIR/coverage-product-objects.json"

jq -r '.data[0].files[].filename' "$PROFILE_DIR/coverage-product-objects.json" \
  | sed "s#^$ROOT/##" \
  | sort > "$PROFILE_DIR/measured-files.txt"

find "${PRODUCT_SOURCE_ROOTS[@]}" -type f \( -name '*.cpp' -o -name '*.hpp' \) \
  | sed "s#^$ROOT/##" \
  | sort > "$PROFILE_DIR/sdk-files.txt"

find "${PRODUCT_SOURCE_ROOTS[@]}" -type f -name '*.cpp' \
  | sed "s#^$ROOT/##" \
  | sort > "$PROFILE_DIR/sdk-cpp-files.txt"

grep -E '\.cpp$' "$PROFILE_DIR/measured-files.txt" > "$PROFILE_DIR/measured-cpp-files.txt" || true
comm -23 "$PROFILE_DIR/sdk-files.txt" "$PROFILE_DIR/measured-files.txt" > "$PROFILE_DIR/unmeasured-sdk-files.txt"
comm -23 "$PROFILE_DIR/sdk-cpp-files.txt" "$PROFILE_DIR/measured-cpp-files.txt" > "$PROFILE_DIR/unmeasured-sdk-cpp-files.txt"

read_json_number() {
  jq -r "$1" "$PROFILE_DIR/coverage-product-objects.json"
}

measured_files=$(wc -l < "$PROFILE_DIR/measured-files.txt" | tr -d ' ')
sdk_files=$(wc -l < "$PROFILE_DIR/sdk-files.txt" | tr -d ' ')
unmeasured_files=$(wc -l < "$PROFILE_DIR/unmeasured-sdk-files.txt" | tr -d ' ')
measured_cpp_files=$(wc -l < "$PROFILE_DIR/measured-cpp-files.txt" | tr -d ' ')
sdk_cpp_files=$(wc -l < "$PROFILE_DIR/sdk-cpp-files.txt" | tr -d ' ')
unmeasured_cpp_files=$(wc -l < "$PROFILE_DIR/unmeasured-sdk-cpp-files.txt" | tr -d ' ')

printf "\nCoverage report written under %s\n" "$PROFILE_DIR"
printf "\nProduct object denominator coverage:\n"
printf "  lines:     %s%% (%s/%s)\n" \
  "$(read_json_number '.data[0].totals.lines.percent')" \
  "$(read_json_number '.data[0].totals.lines.covered')" \
  "$(read_json_number '.data[0].totals.lines.count')"
printf "  functions: %s%% (%s/%s)\n" \
  "$(read_json_number '.data[0].totals.functions.percent')" \
  "$(read_json_number '.data[0].totals.functions.covered')" \
  "$(read_json_number '.data[0].totals.functions.count')"
printf "  branches:  %s%% (%s/%s)\n" \
  "$(read_json_number '.data[0].totals.branches.percent')" \
  "$(read_json_number '.data[0].totals.branches.covered')" \
  "$(read_json_number '.data[0].totals.branches.count')"

printf "\nProduct denominator visibility:\n"
printf "  product object files included: %s\n" "${#objects[@]}"
printf "  measured product files: %s/%s; unmeasured: %s\n" "$measured_files" "$sdk_files" "$unmeasured_files"
printf "  measured product .cpp files: %s/%s; unmeasured: %s\n" "$measured_cpp_files" "$sdk_cpp_files" "$unmeasured_cpp_files"
printf "\nUnmeasured implementation files are listed in:\n"
printf "  %s\n" "$PROFILE_DIR/unmeasured-sdk-cpp-files.txt"
