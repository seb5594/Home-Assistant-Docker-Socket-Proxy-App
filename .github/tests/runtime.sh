#!/usr/bin/env bash
set -Eeuo pipefail

# Runs inside the built image. The proxy is started without a Docker socket,
# which is exactly what an app with Protection mode on sees. Requests it
# refuses get a 403 or 405; requests it lets through find no daemon and get a
# 502 that names the missing socket, so the two outcomes cannot be confused.

PROXY_BIN="${PROXY_BIN:-/usr/local/bin/docker-socket-proxy}"
PROXY=http://127.0.0.1:5594

workdir=$(mktemp -d)
proxy_pid=
cleanup() {
  [[ -z $proxy_pid ]] || kill "$proxy_pid" 2> /dev/null || true
  rm -rf -- "$workdir"
}
trap cleanup EXIT

pass() { echo "PASS: $1"; }
fail() { echo "FAIL: $1" >&2; exit 1; }

# Stores the body in the work directory and prints the HTTP status code.
request() {
  curl --silent --output "$workdir/body" --write-out '%{http_code}' --path-as-is "$@"
}

# expect_status <status> <body fragment> <description> <curl arguments...>
expect_status() {
  local expected=$1 fragment=$2 description=$3 status
  shift 3
  status=$(request "$@")
  [[ $status == "$expected" ]] || fail "$description: expected $expected, got $status ($(cat "$workdir/body"))"
  grep -qF -- "$fragment" "$workdir/body" || fail "$description: body lacks '$fragment' ($(cat "$workdir/body"))"
  pass "$description"
}

# Allowed requests get through the filter and then find no daemon.
expect_allowed() { expect_status 502 'turn off Protection mode' "$@"; }

# Sends raw bytes, for requests curl refuses to build, and prints the status line.
raw_request() {
  exec 3<> /dev/tcp/127.0.0.1/5594
  printf '%b' "$1" >&3
  head -n 1 <&3 | tr -d '\r'
  exec 3>&-
}

expect_raw() {
  local expected=$1 description=$2 status_line
  status_line=$(raw_request "$3")
  [[ $status_line == "HTTP/1.1 $expected"* ]] || fail "$description: expected $expected, got '$status_line'"
  pass "$description"
}

[[ ! -e /var/run/docker.sock ]] || fail 'a Docker socket exists; this test needs none'

"$PROXY_BIN" 2> "$workdir/proxy.log" &
proxy_pid=$!
for _ in {1..50}; do
  curl --silent --fail "$PROXY/healthz" > /dev/null 2>&1 && break
  sleep 0.1
done
grep -qi 'protection mode' "$workdir/proxy.log" || fail 'startup log does not mention Protection mode'
pass 'startup log explains a missing Docker socket'

# --- probes ------------------------------------------------------------------------
expect_status 200 '"status":"ok"' 'health probe answers without the daemon' "$PROXY/healthz"

# --- allowed read-only endpoints ----------------------------------------------------
expect_allowed 'GET /_ping' "$PROXY/_ping"
expect_allowed 'GET /version' "$PROXY/version"
expect_allowed 'GET /info' "$PROXY/info"
expect_allowed 'GET /events with filters' "$PROXY/events?filters=%7B%22type%22%3A%5B%22container%22%5D%7D"
expect_allowed 'GET /containers/json' "$PROXY/containers/json?all=1"
expect_allowed 'GET container details' "$PROXY/containers/home-assistant_1.x/json"
expect_allowed 'GET container stats' "$PROXY/containers/abc123/stats?stream=false"
expect_allowed 'GET /images/json' "$PROXY/images/json"
expect_allowed 'versioned API path' "$PROXY/v1.47/containers/json?all=1"
expect_status 502 'Bad Gateway' 'HEAD /_ping passes the filter' --head "$PROXY/_ping"

# --- everything that changes state is refused ----------------------------------------
expect_status 405 'read-only' 'POST start is refused' --request POST "$PROXY/containers/abc123/start"
expect_status 405 'read-only' 'POST restart is refused' --request POST "$PROXY/v1.47/containers/abc123/restart"
expect_status 405 'read-only' 'POST container create is refused' --request POST "$PROXY/containers/create"
expect_status 405 'read-only' 'DELETE is refused' --request DELETE "$PROXY/containers/abc123"
expect_status 405 'read-only' 'PUT is refused' --request PUT "$PROXY/containers/abc123/archive?path=/"

# --- GET endpoints that expose files or shells are refused ----------------------------
for path in \
  /containers/abc123/logs \
  '/containers/abc123/archive?path=/' \
  /containers/abc123/export \
  /containers/abc123/attach/ws \
  /containers/abc123/top \
  /images/abc123/get \
  /secrets \
  /v1.47/exec/abc123/json \
  /containers/json/ \
  /containers//json \
  /containers/abc123/json/extra \
  /containers/../info \
  '/containers/abc123/json/../archive?path=/' \
  /containers/abc123%2farchive \
  /containers/%2e%2e/json \
  /containers/.hidden/json \
  /v1.x/info \
  /v1./info \
  /v1.47/v1.47/info \
  /info/ \
  /; do
  expect_status 403 'endpoint is not allowed' "GET $path is refused" "$PROXY$path"
done

# --- malformed and hostile requests ---------------------------------------------------
expect_raw 400 'garbage is refused' 'hello\r\n\r\n'
expect_raw 400 'missing HTTP version is refused' 'GET /info\r\n\r\n'
expect_raw 400 'unknown HTTP version is refused' 'GET /info HTTP/2.0\r\n\r\n'
expect_raw 400 'oversized head is refused' "GET /info HTTP/1.1\r\nX-Pad: $(head -c 9000 /dev/zero | tr '\0' a)\r\n\r\n"
expect_raw 403 'control characters in the query are refused' 'GET /info?a=\x01 HTTP/1.1\r\n\r\n'
expect_raw 403 'non-ASCII in the query is refused' 'GET /info?a=\xff HTTP/1.1\r\n\r\n'
expect_raw 403 'NUL byte in the path is refused' 'GET /info\x00/../secrets HTTP/1.1\r\n\r\n'
expect_raw 502 'headers and a second request are ignored' \
  'GET /info HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\nPOST /containers/abc/start HTTP/1.1\r\n\r\n'

# --- logging ----------------------------------------------------------------------------
grep -q '\[REJECTED\] 127.0.0.1 "POST /containers/abc123/start" -> 405' "$workdir/proxy.log" || fail 'rejected request was not logged'
! LC_ALL=C grep -q '[^ -~]' "$workdir/proxy.log" || fail 'log contains control characters'
pass 'rejected requests are logged without control characters'
