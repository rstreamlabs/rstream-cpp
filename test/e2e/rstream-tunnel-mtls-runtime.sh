#!/usr/bin/env bash
# See LICENSE file in the project root for license information.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
PYTHON="${PYTHON:-python3}"
API_URL="${RSTREAM_RUNTIME_API_URL:-http://localhost:3000}"
CONTROL_TOKEN="${RSTREAM_RUNTIME_CONTROL_TOKEN:-${RSTREAM_AUTHENTICATION_TOKEN:-}}"
TIMEOUT_SECONDS="${RSTREAM_TUNNEL_E2E_TIMEOUT:-60}"
NAME_PREFIX="${RSTREAM_TUNNEL_E2E_NAME_PREFIX:-cpp-mtls-$$}"
TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/rstream-cpp-mtls-runtime.XXXXXX")
PASS=0
FAIL=0
PIDS=()
CREDENTIAL_IDS=()
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
  if [ "${#CREDENTIAL_IDS[@]}" -gt 0 ]; then
    RSTREAM_RUNTIME_API_URL="$API_URL" RSTREAM_RUNTIME_CONTROL_TOKEN="$CONTROL_TOKEN" \
      "$PYTHON" "$TMP_DIR/api.py" delete-credentials "${CREDENTIAL_IDS[@]}" >/dev/null 2>&1 || true
  fi
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

find_rstream_tunnel() {
  local candidate
  if [ -n "${RSTREAM_TUNNEL_BIN:-}" ]; then
    printf "%s\n" "$RSTREAM_TUNNEL_BIN"
    return
  fi
  while IFS= read -r candidate; do
    if [ -x "$candidate" ]; then
      printf "%s\n" "$candidate"
      return
    fi
  done < <(find "$ROOT/out" -type f -name rstream-tunnel 2>/dev/null | sort)
  while IFS= read -r candidate; do
    if [ -x "$candidate" ]; then
      printf "%s\n" "$candidate"
      return
    fi
  done < <(find "$ROOT/build" -type f -name rstream-tunnel 2>/dev/null | sort)
  if command -v rstream-tunnel >/dev/null 2>&1; then
    command -v rstream-tunnel
  fi
}

BIN=$(find_rstream_tunnel || true)
if [ -z "$BIN" ] || [ ! -x "$BIN" ]; then
  printf "ERROR missing rstream-tunnel binary; set RSTREAM_TUNNEL_BIN or build the project first\n" >&2
  exit 2
fi
if [ -z "$CONTROL_TOKEN" ]; then
  printf "ERROR set RSTREAM_RUNTIME_CONTROL_TOKEN to a PAT with credential and project read permissions\n" >&2
  exit 2
fi

fail() {
  printf "FAIL %-48s %s\n" "$1" "$2" >&2
  FAIL=$((FAIL + 1))
}

pass() {
  printf "PASS %-48s\n" "$1"
  PASS=$((PASS + 1))
}

cat >"$TMP_DIR/api.py" <<'PY'
import json
import os
import sys
import urllib.error
import urllib.request

api_url = os.environ["RSTREAM_RUNTIME_API_URL"].rstrip("/")
token = os.environ["RSTREAM_RUNTIME_CONTROL_TOKEN"]

def request(method, path, body=None, expect=(200,)):
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(
        api_url + path,
        data=data,
        method=method,
        headers={
            "authorization": "Bearer " + token,
            "content-type": "application/json",
        },
    )
    try:
        with urllib.request.urlopen(req, timeout=20) as resp:
            payload = resp.read().decode()
            if resp.status not in expect:
                raise RuntimeError(f"{method} {path} returned {resp.status}: {payload}")
            return json.loads(payload) if payload else None
    except urllib.error.HTTPError as exc:
        payload = exc.read().decode()
        if exc.code in expect:
            return json.loads(payload) if payload else None
        raise RuntimeError(f"{method} {path} returned {exc.code}: {payload}") from exc

def setup():
    projects = request("GET", "/api/projects/tunnels?pageSize=100")["projects"]
    pro_endpoint = os.environ.get("RSTREAM_RUNTIME_PRO_PROJECT_ENDPOINT", "").strip()
    basic_endpoint = os.environ.get("RSTREAM_RUNTIME_BASIC_PROJECT_ENDPOINT", "").strip()
    pro = next((p for p in projects if p["endpoint"] == pro_endpoint), None) if pro_endpoint else next((p for p in projects if p["plan"] == "pro"), None)
    basic = next((p for p in projects if p["endpoint"] == basic_endpoint), None) if basic_endpoint else next((p for p in projects if p["plan"] == "basic"), None)
    if not pro:
        raise RuntimeError("no Pro project found; set RSTREAM_RUNTIME_PRO_PROJECT_ENDPOINT")
    if not basic:
        raise RuntimeError("no Basic project found; set RSTREAM_RUNTIME_BASIC_PROJECT_ENDPOINT")
    with open(os.environ["RSTREAM_RUNTIME_CERT"], encoding="utf-8") as stream:
        cert = stream.read()
    credential = request("POST", "/api/credentials", {
        "type": "mtls",
        "name": os.environ["RSTREAM_RUNTIME_NAME_PREFIX"] + "-cpp-mtls",
        "certificatePem": cert,
        "permissionPolicy": {
            "control": {"mode": "none", "permissions": []},
            "engine": {
                "mode": "select",
                "permissions": [
                    "tunnels.tunnels.create-delete",
                    "tunnels.streams.create-delete",
                    "tunnels.resources.read-only",
                ],
            },
            "turn": {"mode": "none", "permissions": []},
        },
        "resources": {"tunnels": {"projects": [pro["id"]]}},
    })
    print(json.dumps({"credentialId": credential["id"], "pro": pro, "basic": basic}))

def delete_credentials(ids):
    for credential_id in ids:
        try:
            request("DELETE", "/api/credentials/" + credential_id, expect=(200, 204, 404))
        except RuntimeError:
            pass

command = sys.argv[1]
if command == "setup":
    setup()
elif command == "delete-credentials":
    delete_credentials(sys.argv[2:])
else:
    raise SystemExit(f"unknown command: {command}")
PY

json_get() {
  "$PYTHON" - "$1" "$2" <<'PY'
import json
import sys
with open(sys.argv[1], encoding="utf-8") as stream:
    value = json.load(stream)
for part in sys.argv[2].split("."):
    value = value[part]
print(value)
PY
}

make_cert() {
  openssl req -x509 -newkey rsa:2048 -nodes -days 1 \
    -subj "/CN=$1" \
    -keyout "$2" \
    -out "$3" >/dev/null 2>&1
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
      fail "$label" "upstream exited early"
      tail -20 "$log" >&2 || true
      return 1
    fi
    sleep 0.2
  done
  fail "$label" "upstream did not become ready"
  tail -20 "$log" >&2 || true
  return 1
}

start_upstream() {
  local label=$1
  local log="$TMP_DIR/upstream-$label.log"
  "$PYTHON" "$ROOT/test/e2e/runtime_harness.py" serve http >"$log" 2>&1 &
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
        if event.get("type") == "tunnel_status" and event.get("forwarding"):
            print(event["forwarding"])
            sys.exit(0)
sys.exit(1)
PY
}

start_tunnel() {
  local label=$1 engine=$2 cert=$3 key=$4
  shift 4
  TUNNEL_LOG="$TMP_DIR/tunnel-$label.log"
  : >"$TUNNEL_LOG"
  env -u RSTREAM_CONTEXT -u RSTREAM_AUTHENTICATION_TOKEN -u RSTREAM_ENGINE_ADDRESS \
    RSTREAM_ENGINE="$engine" \
    RSTREAM_MTLS_CERT_FILE="$cert" \
    RSTREAM_MTLS_KEY_FILE="$key" \
    "$BIN" "$UPSTREAM_ADDR" --format json --no-retry "$@" >"$TUNNEL_LOG" 2>&1 &
  TUNNEL_PID=$!
  PIDS+=("$TUNNEL_PID")
  local deadline=$((SECONDS + TIMEOUT_SECONDS))
  while [ "$SECONDS" -lt "$deadline" ]; do
    if FORWARDING=$(extract_forwarding "$TUNNEL_LOG"); then
      return 0
    fi
    if ! kill -0 "$TUNNEL_PID" 2>/dev/null; then
      tail -40 "$TUNNEL_LOG" >&2 || true
      return 1
    fi
    if grep -Eiq "invalid request|a fatal error occurred|tunnel creation failed|Unauthorized|Forbidden" "$TUNNEL_LOG"; then
      tail -40 "$TUNNEL_LOG" >&2 || true
      return 1
    fi
    sleep 0.2
  done
  tail -40 "$TUNNEL_LOG" >&2 || true
  return 1
}

stop_tunnel() {
  kill "$TUNNEL_PID" 2>/dev/null || true
  wait "$TUNNEL_PID" 2>/dev/null || true
}

make_cert "$NAME_PREFIX-client" "$TMP_DIR/client.key" "$TMP_DIR/client.crt"
export RSTREAM_RUNTIME_API_URL="$API_URL"
export RSTREAM_RUNTIME_CONTROL_TOKEN="$CONTROL_TOKEN"
export RSTREAM_RUNTIME_NAME_PREFIX="$NAME_PREFIX"
export RSTREAM_RUNTIME_CERT="$TMP_DIR/client.crt"
"$PYTHON" "$TMP_DIR/api.py" setup >"$TMP_DIR/setup.json"
CREDENTIAL_IDS+=("$(json_get "$TMP_DIR/setup.json" credentialId)")
PRO_ENGINE="$(json_get "$TMP_DIR/setup.json" pro.url)"
BASIC_ENGINE="$(json_get "$TMP_DIR/setup.json" basic.url)"

start_upstream "cpp-mtls"
if start_tunnel "cpp-mtls" "$PRO_ENGINE" "$TMP_DIR/client.crt" "$TMP_DIR/client.key" --http --publish --mtls --name "$NAME_PREFIX-published"; then
  if "$PYTHON" "$ROOT/test/e2e/runtime_harness.py" check https-ping --addr "$FORWARDING" >/dev/null 2>&1; then
    fail "C++ published mTLS rejects missing client certificate" "request without certificate succeeded"
  else
    pass "C++ published mTLS rejects missing client certificate"
  fi
  if "$PYTHON" "$ROOT/test/e2e/runtime_harness.py" check https-ping --addr "$FORWARDING" --cert "$TMP_DIR/client.crt" --key "$TMP_DIR/client.key"; then
    pass "C++ agent mTLS creates published mTLS tunnel"
  else
    fail "C++ agent mTLS creates published mTLS tunnel" "request with registered certificate failed"
  fi
  status=$(curl -sk --cert "$TMP_DIR/client.crt" --key "$TMP_DIR/client.key" -b "rstream_auth=session" -o "$TMP_DIR/rstream-conflict-body.txt" -w "%{http_code}" "$FORWARDING/ping" || true)
  if [ "$status" = "200" ]; then
    fail "C++ published mTLS rejects rstream Auth conflict" "request with certificate and rstream_auth cookie succeeded"
  else
    pass "C++ published mTLS rejects rstream Auth conflict"
  fi
  stop_tunnel
else
  fail "C++ agent mTLS creates published mTLS tunnel" "tunnel did not become ready"
fi

start_upstream "cpp-mtls-rstream-auth"
if start_tunnel "cpp-mtls-rstream-auth" "$PRO_ENGINE" "$TMP_DIR/client.crt" "$TMP_DIR/client.key" --http --publish --mtls --rstream-auth --name "$NAME_PREFIX-rstream-auth"; then
  if "$PYTHON" "$ROOT/test/e2e/runtime_harness.py" check https-ping --addr "$FORWARDING" --cert "$TMP_DIR/client.crt" --key "$TMP_DIR/client.key"; then
    pass "C++ published mTLS and rstream Auth accepts certificate-only request"
  else
    fail "C++ published mTLS and rstream Auth accepts certificate-only request" "certificate-only request failed"
  fi
  status=$(curl -sk --cert "$TMP_DIR/client.crt" --key "$TMP_DIR/client.key" -b "rstream_auth=session" -o "$TMP_DIR/combined-rstream-conflict-body.txt" -w "%{http_code}" "$FORWARDING/ping" || true)
  if [ "$status" = "200" ]; then
    fail "C++ published mTLS and rstream Auth rejects combined proofs" "request with certificate and rstream_auth cookie succeeded"
  else
    pass "C++ published mTLS and rstream Auth rejects combined proofs"
  fi
  stop_tunnel
else
  fail "C++ published mTLS and rstream Auth tunnel creation" "tunnel did not become ready"
fi

if env -u RSTREAM_CONTEXT -u RSTREAM_AUTHENTICATION_TOKEN -u RSTREAM_ENGINE_ADDRESS \
  RSTREAM_ENGINE="$BASIC_ENGINE" \
  RSTREAM_MTLS_CERT_FILE="$TMP_DIR/client.crt" \
  RSTREAM_MTLS_KEY_FILE="$TMP_DIR/client.key" \
  "$BIN" "$UPSTREAM_ADDR" --format json --no-retry --http --publish --name "$NAME_PREFIX-denied" >/dev/null 2>&1; then
  fail "C++ mTLS project grant denies another project" "Basic project accepted Pro-only credential"
else
  pass "C++ mTLS project grant denies another project"
fi

if env -u RSTREAM_CONTEXT -u RSTREAM_ENGINE_ADDRESS \
  RSTREAM_ENGINE="$PRO_ENGINE" \
  RSTREAM_AUTHENTICATION_TOKEN="conflict" \
  RSTREAM_MTLS_CERT_FILE="$TMP_DIR/client.crt" \
  RSTREAM_MTLS_KEY_FILE="$TMP_DIR/client.key" \
  "$BIN" "$UPSTREAM_ADDR" --format json --no-retry --http --publish --name "$NAME_PREFIX-conflict" >"$TMP_DIR/conflict.log" 2>&1; then
  fail "C++ rejects token and mTLS agent auth conflict" "conflicting auth succeeded"
elif grep -q "token and mTLS authentication cannot be used together" "$TMP_DIR/conflict.log"; then
  pass "C++ rejects token and mTLS agent auth conflict"
else
  fail "C++ rejects token and mTLS agent auth conflict" "unexpected error: $(tail -1 "$TMP_DIR/conflict.log")"
fi

printf "\nResults: %d passed, %d failed\n" "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ]
