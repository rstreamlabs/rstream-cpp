#!/usr/bin/env bash
# See LICENSE file in the project root for license information.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
PYTHON="${PYTHON:-python3}"
API_URL="${RSTREAM_RUNTIME_API_URL:-}"
CONTROL_TOKEN="${RSTREAM_RUNTIME_CONTROL_TOKEN:-}"
TIMEOUT_SECONDS="${RSTREAM_TUNNEL_E2E_TIMEOUT:-60}"
NAME_PREFIX="${RSTREAM_TUNNEL_E2E_NAME_PREFIX:-cpp-pkcs11-$$}"
TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/rstream-cpp-pkcs11-runtime.XXXXXX")
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

fail() {
  printf "FAIL %-48s %s\n" "$1" "$2" >&2
  FAIL=$((FAIL + 1))
}

pass() {
  printf "PASS %-48s\n" "$1"
  PASS=$((PASS + 1))
}

first_existing_file() {
  local candidate
  for candidate in "$@"; do
    if [ -f "$candidate" ]; then
      printf "%s\n" "$candidate"
      return 0
    fi
  done
  return 1
}

first_existing_dir() {
  local candidate
  for candidate in "$@"; do
    if [ -d "$candidate" ]; then
      printf "%s\n" "$candidate"
      return 0
    fi
  done
  return 1
}

find_openssl() {
  local candidate
  if [ -n "${RSTREAM_RUNTIME_OPENSSL_BIN:-}" ]; then
    printf "%s\n" "$RSTREAM_RUNTIME_OPENSSL_BIN"
    return
  fi
  for candidate in \
    /opt/homebrew/opt/openssl@3/bin/openssl \
    /usr/local/opt/openssl@3/bin/openssl \
    /opt/homebrew/bin/openssl \
    /usr/local/bin/openssl; do
    if [ -x "$candidate" ] && "$candidate" list -providers >/dev/null 2>&1; then
      printf "%s\n" "$candidate"
      return
    fi
  done
  if command -v openssl >/dev/null 2>&1; then
    candidate=$(command -v openssl)
    if "$candidate" list -providers >/dev/null 2>&1; then
      printf "%s\n" "$candidate"
    fi
  fi
}

first_working_openssl_provider() {
  local candidate
  for candidate in "$@"; do
    if "$OPENSSL_BIN" list -providers -provider default -provider "$candidate" >/dev/null 2>&1; then
      printf "%s\n" "$candidate"
      return 0
    fi
  done
  return 1
}

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
if [ -z "$API_URL" ]; then
  printf "ERROR set RSTREAM_RUNTIME_API_URL to the Control plane API URL for this test\n" >&2
  printf "This runtime suite is not engine-only; it creates Control plane credential resources.\n" >&2
  exit 2
fi
if [ -z "$CONTROL_TOKEN" ]; then
  printf "ERROR set RSTREAM_RUNTIME_CONTROL_TOKEN to a PAT with credential and project read permissions\n" >&2
  printf "Do not rely on the engine context token for Control plane setup checks.\n" >&2
  exit 2
fi
OPENSSL_BIN=$(find_openssl || true)
if [ -z "$OPENSSL_BIN" ] || [ ! -x "$OPENSSL_BIN" ]; then
  printf "ERROR missing OpenSSL 3 binary with provider support; set RSTREAM_RUNTIME_OPENSSL_BIN\n" >&2
  exit 2
fi

for command in pkcs11-tool softhsm2-util; do
  if ! command -v "$command" >/dev/null 2>&1; then
    printf "ERROR missing command: %s\n" "$command" >&2
    exit 2
  fi
done

PKCS11_MODULE="${RSTREAM_TEST_PKCS11_MODULE:-${RSTREAM_RUNTIME_PKCS11_MODULE:-}}"
if [ -z "$PKCS11_MODULE" ]; then
  PKCS11_MODULE=$(first_existing_file \
    /opt/homebrew/lib/softhsm/libsofthsm2.so \
    /usr/local/lib/softhsm/libsofthsm2.so \
    /usr/lib/softhsm/libsofthsm2.so \
    /usr/lib/x86_64-linux-gnu/softhsm/libsofthsm2.so \
    /usr/lib64/pkcs11/libsofthsm2.so || true)
fi
if [ -z "$PKCS11_MODULE" ] || [ ! -f "$PKCS11_MODULE" ]; then
  printf "ERROR missing SoftHSM PKCS#11 module; set RSTREAM_RUNTIME_PKCS11_MODULE\n" >&2
  exit 2
fi

OPENSSL_MODULES_DIR="${OPENSSL_MODULES:-}"
if [ -z "$OPENSSL_MODULES_DIR" ]; then
  OPENSSL_MODULES_DIR=$(first_existing_dir \
    /opt/homebrew/lib/ossl-modules \
    /usr/local/lib/ossl-modules \
    /usr/lib64/ossl-modules \
    /usr/lib/x86_64-linux-gnu/ossl-modules \
    /usr/lib/ssl/ossl-modules || true)
fi
if [ -n "$OPENSSL_MODULES_DIR" ]; then
  export OPENSSL_MODULES="$OPENSSL_MODULES_DIR"
fi

OPENSSL_ENGINES_DIR="${OPENSSL_ENGINES:-}"
if [ -z "$OPENSSL_ENGINES_DIR" ]; then
  OPENSSL_ENGINES_DIR=$(first_existing_dir \
    /opt/homebrew/lib/engines-3 \
    /usr/local/lib/engines-3 \
    /usr/lib64/engines-3 \
    /usr/lib/x86_64-linux-gnu/engines-3 || true)
fi
if [ -n "$OPENSSL_ENGINES_DIR" ]; then
  export OPENSSL_ENGINES="$OPENSSL_ENGINES_DIR"
fi

OPENSSL_PKCS11_PROVIDER="${OPENSSL_PKCS11_PROVIDER:-${RSTREAM_RUNTIME_OPENSSL_PKCS11_PROVIDER:-}}"
if [ -z "$OPENSSL_PKCS11_PROVIDER" ]; then
  OPENSSL_PKCS11_PROVIDER=$(first_working_openssl_provider pkcs11prov pkcs11 || true)
fi
if [ -z "$OPENSSL_PKCS11_PROVIDER" ]; then
  printf "ERROR missing OpenSSL PKCS#11 provider; set OPENSSL_PKCS11_PROVIDER\n" >&2
  exit 2
fi

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
    pro = next((p for p in projects if p["endpoint"] == pro_endpoint), None) if pro_endpoint else next((p for p in projects if p["plan"] == "pro"), None)
    if not pro:
        raise RuntimeError("no Pro project found; set RSTREAM_RUNTIME_PRO_PROJECT_ENDPOINT")
    with open(os.environ["RSTREAM_RUNTIME_CERT"], encoding="utf-8") as stream:
        cert = stream.read()
    credential = request("POST", "/api/credentials", {
        "type": "mtls",
        "name": os.environ["RSTREAM_RUNTIME_NAME_PREFIX"] + "-cpp-pkcs11",
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
    print(json.dumps({"credentialId": credential["id"], "pro": pro}))

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

start_pkcs11_tunnel() {
  local label=$1
  TUNNEL_LOG="$TMP_DIR/tunnel-$label.log"
  : >"$TUNNEL_LOG"
  env -u RSTREAM_AUTHENTICATION_TOKEN -u RSTREAM_ENGINE -u RSTREAM_ENGINE_ADDRESS \
    RSTREAM_CONFIG="$TMP_DIR/config.yaml" \
    RSTREAM_CONTEXT=pkcs11 \
    RSTREAM_PKCS11_PIN="$PKCS11_PIN" \
    "$BIN" "$UPSTREAM_ADDR" --format json --no-retry --http --publish --name "$NAME_PREFIX-published" >"$TUNNEL_LOG" 2>&1 &
  TUNNEL_PID=$!
  PIDS+=("$TUNNEL_PID")
  local deadline=$((SECONDS + TIMEOUT_SECONDS))
  while [ "$SECONDS" -lt "$deadline" ]; do
    if FORWARDING=$(extract_forwarding "$TUNNEL_LOG"); then
      return 0
    fi
    if ! kill -0 "$TUNNEL_PID" 2>/dev/null; then
      tail -60 "$TUNNEL_LOG" >&2 || true
      return 1
    fi
    if grep -Eiq "invalid request|a fatal error occurred|tunnel creation failed|Unauthorized|Forbidden" "$TUNNEL_LOG"; then
      tail -60 "$TUNNEL_LOG" >&2 || true
      return 1
    fi
    sleep 0.2
  done
  tail -60 "$TUNNEL_LOG" >&2 || true
  return 1
}

stop_tunnel() {
  kill "$TUNNEL_PID" 2>/dev/null || true
  wait "$TUNNEL_PID" 2>/dev/null || true
}

export SOFTHSM2_CONF="$TMP_DIR/softhsm2.conf"
mkdir -p "$TMP_DIR/tokens"
cat >"$SOFTHSM2_CONF" <<EOF
directories.tokendir = $TMP_DIR/tokens
objectstore.backend = file
log.level = ERROR
slots.removable = false
EOF

PKCS11_PIN="${RSTREAM_TEST_PKCS11_PIN:-123456}"
TOKEN_LABEL="RSTREAM-${NAME_PREFIX}"
KEY_LABEL="rstream-client-${NAME_PREFIX}"
softhsm2-util --init-token --free --label "$TOKEN_LABEL" --pin "$PKCS11_PIN" --so-pin 12345678 >/dev/null
pkcs11-tool --module "$PKCS11_MODULE" --login --pin "$PKCS11_PIN" --keypairgen --key-type EC:prime256v1 --label "$KEY_LABEL" --id 01 --usage-sign >/dev/null

export PKCS11_MODULE_PATH="$PKCS11_MODULE"
export PKCS11_PIN
OPENSSL_PKCS11_ARGS=()
if [ "$OPENSSL_PKCS11_PROVIDER" = "pkcs11" ]; then
  cat >"$TMP_DIR/openssl-pkcs11.cnf" <<EOF
openssl_conf = openssl_init
[openssl_init]
providers = provider_sect
[provider_sect]
default = default_sect
pkcs11 = pkcs11_sect
[default_sect]
activate = 1
[pkcs11_sect]
module = $OPENSSL_MODULES/pkcs11.so
pkcs11-module-path = $PKCS11_MODULE
pkcs11-module-token-pin = $PKCS11_PIN
activate = 1
EOF
  export OPENSSL_CONF="$TMP_DIR/openssl-pkcs11.cnf"
else
  OPENSSL_PKCS11_ARGS=(-provider "$OPENSSL_PKCS11_PROVIDER" -provider default)
fi
"$OPENSSL_BIN" req \
  -new \
  -x509 \
  "${OPENSSL_PKCS11_ARGS[@]}" \
  -key "pkcs11:token=$TOKEN_LABEL;object=$KEY_LABEL;type=private" \
  -sha256 \
  -days 1 \
  -subj "/CN=$NAME_PREFIX" \
  -out "$TMP_DIR/client.crt" >/dev/null

export RSTREAM_RUNTIME_API_URL="$API_URL"
export RSTREAM_RUNTIME_CONTROL_TOKEN="$CONTROL_TOKEN"
export RSTREAM_RUNTIME_NAME_PREFIX="$NAME_PREFIX"
export RSTREAM_RUNTIME_CERT="$TMP_DIR/client.crt"
"$PYTHON" "$TMP_DIR/api.py" setup >"$TMP_DIR/setup.json"
CREDENTIAL_IDS+=("$(json_get "$TMP_DIR/setup.json" credentialId)")
PRO_ENGINE="$(json_get "$TMP_DIR/setup.json" pro.url)"

cat >"$TMP_DIR/config.yaml" <<EOF
version: 1
defaults:
  context:
    name: pkcs11
contexts:
  - name: pkcs11
    engine: "$PRO_ENGINE"
    auth:
      mtls:
        storage:
          kind: pkcs11
          module: "$PKCS11_MODULE"
          opensslProvider: "$OPENSSL_PKCS11_PROVIDER"
          tokenLabel: "$TOKEN_LABEL"
          keyLabel: "$KEY_LABEL"
          certificateFile: "$TMP_DIR/client.crt"
          pinEnv: RSTREAM_PKCS11_PIN
EOF

start_upstream "cpp-pkcs11"
if start_pkcs11_tunnel "cpp-pkcs11"; then
  if "$PYTHON" "$ROOT/test/e2e/runtime_harness.py" check https-ping --addr "$FORWARDING"; then
    pass "C++ PKCS#11 mTLS creates published HTTP tunnel"
  else
    fail "C++ PKCS#11 mTLS creates published HTTP tunnel" "published endpoint did not reach upstream"
  fi
  stop_tunnel
else
  fail "C++ PKCS#11 mTLS creates published HTTP tunnel" "tunnel did not become ready"
fi

printf "\nResults: %d passed, %d failed\n" "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ]
