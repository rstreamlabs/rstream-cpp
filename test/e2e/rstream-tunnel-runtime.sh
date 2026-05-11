#!/usr/bin/env bash
# See LICENSE file in the project root for license information.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
PYTHON="${PYTHON:-python3}"
TIMEOUT_SECONDS="${RSTREAM_TUNNEL_E2E_TIMEOUT:-60}"
NAME_PREFIX="${RSTREAM_TUNNEL_E2E_NAME_PREFIX:-cpp-runtime-$$}"
TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/rstream-cpp-runtime.XXXXXX")
PASS=0
FAIL=0
PIDS=()
UPSTREAM_ADDR=
TUNNEL_PID=
FORWARDING=
TUNNEL_LOG=

cleanup() {
  local pid
  for pid in "${PIDS[@]:-}"; do
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
  done
  rm -rf "$TMP_DIR"
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

make_cert() {
  openssl req -x509 -newkey rsa:2048 -nodes -days 1 \
    -subj "/CN=localhost" \
    -keyout "$TMP_DIR/upstream.key" \
    -out "$TMP_DIR/upstream.crt" >/dev/null 2>&1
}

wait_ready() {
  local pid=$1 log=$2 label=$3
  local deadline=$((SECONDS + TIMEOUT_SECONDS))
  while [ "$SECONDS" -lt "$deadline" ]; do
    if grep -q "^READY " "$log" 2>/dev/null; then
      awk '/^READY / {print $2; exit}' "$log"
      return 0
    fi
    if ! kill -0 "$pid" 2>/dev/null; then
      printf "FAIL %-38s upstream exited early\n" "$label" >&2
      tail -20 "$log" >&2 || true
      return 1
    fi
    sleep 0.2
  done
  printf "FAIL %-38s upstream did not become ready\n" "$label" >&2
  tail -20 "$log" >&2 || true
  return 1
}

start_upstream() {
  local label=$1 mode=$2
  local log="$TMP_DIR/upstream-$label.log"
  case "$mode" in
    tcp|http)
      "$PYTHON" "$ROOT/test/e2e/runtime_harness.py" serve "$mode" >"$log" 2>&1 &
      ;;
    tls)
      "$PYTHON" "$ROOT/test/e2e/runtime_harness.py" serve tls \
        --cert "$TMP_DIR/upstream.crt" \
        --key "$TMP_DIR/upstream.key" >"$log" 2>&1 &
      ;;
    *)
      printf "ERROR unknown upstream mode: %s\n" "$mode" >&2
      exit 2
      ;;
  esac
  local pid=$!
  PIDS+=("$pid")
  UPSTREAM_ADDR=$(wait_ready "$pid" "$log" "$label")
}

extract_forwarding() {
  "$PYTHON" - "$1" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    for line in stream:
        try:
            event = json.loads(line)
        except json.JSONDecodeError:
            continue
        if event.get("type") == "tunnel_status" and event.get("tunnel_id") and event.get("forwarding"):
            print(event["forwarding"])
            sys.exit(0)
sys.exit(1)
PY
}

start_tunnel() {
  local label=$1 target=$2
  shift 2
  TUNNEL_LOG="$TMP_DIR/tunnel-$label.log"
  : >"$TUNNEL_LOG"
  "$BIN" "$target" --format json --no-retry "$@" >"$TUNNEL_LOG" 2>&1 &
  TUNNEL_PID=$!
  PIDS+=("$TUNNEL_PID")

  local deadline=$((SECONDS + TIMEOUT_SECONDS))
  while [ "$SECONDS" -lt "$deadline" ]; do
    if FORWARDING=$(extract_forwarding "$TUNNEL_LOG"); then
      return 0
    fi
    if ! kill -0 "$TUNNEL_PID" 2>/dev/null; then
      printf "FAIL %-38s tunnel exited early\n" "$label" >&2
      tail -40 "$TUNNEL_LOG" >&2 || true
      return 1
    fi
    if grep -Eiq "invalid request|a fatal error occurred|tunnel creation failed" "$TUNNEL_LOG"; then
      printf "FAIL %-38s tunnel reported an error\n" "$label" >&2
      tail -40 "$TUNNEL_LOG" >&2 || true
      return 1
    fi
    sleep 0.2
  done
  printf "FAIL %-38s tunnel did not become ready\n" "$label" >&2
  tail -40 "$TUNNEL_LOG" >&2 || true
  return 1
}

stop_pid() {
  local pid=$1
  kill "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
}

run_case() {
  local label=$1
  shift
  if "$@"; then
    printf "PASS %-38s\n" "$label"
    PASS=$((PASS + 1))
  else
    printf "FAIL %-38s\n" "$label" >&2
    FAIL=$((FAIL + 1))
  fi
}

assert_rejects_passthrough_policy() {
  local label=$1
  shift

  local log="$TMP_DIR/reject-$label.log"
  if "$BIN" 127.0.0.1:65535 --tls --tls-mode passthrough --no-retry --format json \
    --name "$NAME_PREFIX-reject-$label" "$@" >"$log" 2>&1; then
    printf "invalid TLS passthrough options were accepted\n" >&2
    cat "$log" >&2
    return 1
  fi
  if ! grep -q "TLS passthrough cannot be combined" "$log"; then
    printf "rejection did not explain the TLS passthrough policy\n" >&2
    cat "$log" >&2
    return 1
  fi
}

case_reject_passthrough_policy() {
  assert_rejects_passthrough_policy "tls-min-version" --tls-min-version tls1.2
  assert_rejects_passthrough_policy "upstream-tls" --upstream-tls
  assert_rejects_passthrough_policy "tls-alpn" --tls-alpn rstream-runtime-stream
}

case_tls_terminated() {
  local upstream
  local rc=0
  start_upstream "tls-terminated" tcp
  upstream=$UPSTREAM_ADDR
  start_tunnel "tls-terminated" "$upstream" \
    --tls --tls-mode terminated --tls-alpn rstream-runtime-stream \
    --name "$NAME_PREFIX-tls-terminated"
  "$PYTHON" "$ROOT/test/e2e/runtime_harness.py" check tls-echo \
    --addr "$FORWARDING" --alpn rstream-runtime-stream || rc=$?
  stop_pid "$TUNNEL_PID"
  return "$rc"
}

case_tls_upstream_tls() {
  local upstream
  local rc=0
  start_upstream "tls-upstream-tls" tls
  upstream=$UPSTREAM_ADDR
  start_tunnel "tls-upstream-tls" "$upstream" \
    --tls --tls-mode terminated --upstream-tls --tls-alpn rstream-runtime-stream \
    --name "$NAME_PREFIX-tls-upstream"
  "$PYTHON" "$ROOT/test/e2e/runtime_harness.py" check tls-echo \
    --addr "$FORWARDING" --alpn rstream-runtime-stream || rc=$?
  stop_pid "$TUNNEL_PID"
  return "$rc"
}

case_tls_passthrough() {
  local upstream
  local rc=0
  start_upstream "tls-passthrough" tls
  upstream=$UPSTREAM_ADDR
  start_tunnel "tls-passthrough" "$upstream" \
    --tls --tls-mode passthrough --name "$NAME_PREFIX-tls-passthrough"
  "$PYTHON" "$ROOT/test/e2e/runtime_harness.py" check tls-echo --addr "$FORWARDING" || rc=$?
  stop_pid "$TUNNEL_PID"
  return "$rc"
}

case_http_h1() {
  local upstream
  local rc=0
  start_upstream "http-h1" http
  upstream=$UPSTREAM_ADDR
  start_tunnel "http-h1" "$upstream" --http --name "$NAME_PREFIX-http-h1"
  "$PYTHON" "$ROOT/test/e2e/runtime_harness.py" check https-ping --addr "$FORWARDING" || rc=$?
  stop_pid "$TUNNEL_PID"
  return "$rc"
}

make_cert
run_case "reject invalid passthrough policy" case_reject_passthrough_policy
run_case "rstream-tunnel tls terminated" case_tls_terminated
run_case "rstream-tunnel tls upstream tls" case_tls_upstream_tls
run_case "rstream-tunnel tls passthrough" case_tls_passthrough
run_case "rstream-tunnel http h1" case_http_h1

printf "\nResults: %d passed, %d failed\n" "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ]
