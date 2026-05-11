#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
TARGET=${RSTREAM_TUNNEL_E2E_TARGET:-127.0.0.1:65535}
TIMEOUT_SECONDS=${RSTREAM_TUNNEL_E2E_TIMEOUT:-30}
TEST_NAME="cpp-e2e-tls-passthrough-$$"
LOG_FILE=$(mktemp "${TMPDIR:-/tmp}/rstream-cpp-tunnel-e2e.XXXXXX")
TUNNEL_PID=

cleanup() {
  if [ -n "${TUNNEL_PID:-}" ]; then
    kill "$TUNNEL_PID" 2>/dev/null || true
    wait "$TUNNEL_PID" 2>/dev/null || true
  fi
  rm -f "$LOG_FILE"
}
trap cleanup EXIT

find_rstream_tunnel() {
  if [ -n "${RSTREAM_TUNNEL_BIN:-}" ]; then
    printf "%s\n" "$RSTREAM_TUNNEL_BIN"
    return
  fi

  local candidate
  for candidate in \
    "$ROOT/build-$(uname -m)-$(uname -s)$(uname -r)/release/bin/rstream-tunnel" \
    "$ROOT/build/release/bin/rstream-tunnel" \
    "$ROOT/build/bin/rstream-tunnel"; do
    if [ -x "$candidate" ]; then
      printf "%s\n" "$candidate"
      return
    fi
  done

  while IFS= read -r candidate; do
    if [ -x "$candidate" ]; then
      printf "%s\n" "$candidate"
      return
    fi
  done < <(find "$ROOT" -path "*/bin/rstream-tunnel" -type f 2>/dev/null | sort)
}

BIN=$(find_rstream_tunnel)
if [ -z "$BIN" ] || [ ! -x "$BIN" ]; then
  printf "ERROR missing rstream-tunnel binary; set RSTREAM_TUNNEL_BIN or build the project first\n" >&2
  exit 2
fi

assert_rejects_passthrough_policy() {
  local label=$1
  shift

  : > "$LOG_FILE"
  if "$BIN" "$TARGET" --tls --tls-mode passthrough --no-retry --format json --name "$TEST_NAME-$label" "$@" >"$LOG_FILE" 2>&1; then
    printf "FAIL %s: invalid TLS passthrough options were accepted\n" "$label" >&2
    cat "$LOG_FILE" >&2
    exit 1
  fi
  if ! grep -q "TLS passthrough cannot be combined" "$LOG_FILE"; then
    printf "FAIL %s: rejection did not explain the TLS passthrough policy\n" "$label" >&2
    cat "$LOG_FILE" >&2
    exit 1
  fi
  printf "PASS reject %s\n" "$label"
}

assert_rejects_passthrough_policy "tls-min-version" --tls-min-version tls1.2
assert_rejects_passthrough_policy "upstream-tls" --upstream-tls
assert_rejects_passthrough_policy "tls-alpn" --tls-alpn rstream-stream-echo

: > "$LOG_FILE"
"$BIN" "$TARGET" --tls --tls-mode passthrough --no-retry --format json --name "$TEST_NAME" >"$LOG_FILE" 2>&1 &
TUNNEL_PID=$!

deadline=$((SECONDS + TIMEOUT_SECONDS))
while [ "$SECONDS" -lt "$deadline" ]; do
  if grep -q '"type":"tunnel_status"' "$LOG_FILE" && grep -q '"tunnel_id":' "$LOG_FILE" && grep -q '"forwarding":' "$LOG_FILE"; then
    printf "PASS create tls passthrough tunnel\n"
    exit 0
  fi
  if ! kill -0 "$TUNNEL_PID" 2>/dev/null; then
    printf "FAIL tunnel process exited before publishing a passthrough tunnel\n" >&2
    cat "$LOG_FILE" >&2
    exit 1
  fi
  if grep -Eiq "invalid request|a fatal error occurred|tunnel creation failed" "$LOG_FILE"; then
    printf "FAIL passthrough tunnel creation failed\n" >&2
    cat "$LOG_FILE" >&2
    exit 1
  fi
  sleep 0.2
done

printf "FAIL timed out waiting for passthrough tunnel creation\n" >&2
cat "$LOG_FILE" >&2
exit 1
