#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# Packaging-only check. Run in an isolated --network none container; localhost
# cannot be a real printer. No live credentials or hardware are used.
set -eu
export BAMBU_DEV_ID=TEST_DEVICE BAMBU_DEV_IP=127.0.0.1
export BAMBU_ACCESS_CODE=TEST_ONLY_ACCESS
export BAMBU_HTTP_TOKEN=TEST_ONLY_TOKEN_000000000000000000000000
if [ "${1:-standalone}" = home-assistant ]; then
    export BFB_HOME_ASSISTANT=1
    mkdir -p /data
    jq -n --arg printer_id "$BAMBU_DEV_ID" --arg printer_ip "$BAMBU_DEV_IP" \
      --arg access_code "$BAMBU_ACCESS_CODE" --arg http_token "$BAMBU_HTTP_TOKEN" \
      '{printer_id:$printer_id,printer_ip:$printer_ip,access_code:$access_code,http_token:$http_token}' >/data/options.json
    chmod 600 /data/options.json
fi
/usr/local/bin/bfb-entrypoint bambu-bridge >/tmp/bfb-smoke.log 2>&1 &
pid=$!
trap 'kill -TERM "$pid" 2>/dev/null || true' EXIT
code=$(curl -s --retry 10 --retry-delay 1 --retry-connrefused --max-time 3 \
    -o /tmp/health.json -w '%{http_code}' http://127.0.0.1:8080/health)
[ "$code" = 503 ]
jq -e '.status == "not_ready" and .ready == false and .connected == false and .printerId == "TEST_DEVICE"' /tmp/health.json >/dev/null
awk '/^Uid:/ { if ($2 != 10001 || $3 != 10001) exit 1; found=1 } END { if (!found) exit 1 }' /proc/"$pid"/status
code=$(curl -s --max-time 3 -o /tmp/cap.json -w '%{http_code}' http://127.0.0.1:8080/api/v1/capabilities)
[ "$code" = 401 ]
code=$(curl -s --max-time 3 -H "Authorization: Bearer $BAMBU_HTTP_TOKEN" \
    -o /tmp/cap.json -w '%{http_code}' http://127.0.0.1:8080/api/v1/capabilities)
[ "$code" = 200 ]
jq -e '.apiVersion == 1 and .backend == "open-bamboo-networking" and .features.amsFilamentWrite == true and .features.externalFilamentWrite == false' /tmp/cap.json >/dev/null
code=$(curl -s --max-time 3 -H "Authorization: Bearer $BAMBU_HTTP_TOKEN" \
    -H 'Content-Type: application/json' \
    --data '{"profile":"TEST_PROFILE","setting":"TEST_SETTING","type":"PETG","color":"808080FF","tempMin":220,"tempMax":260}' \
    -o /tmp/write.json -w '%{http_code}' http://127.0.0.1:8080/api/v1/ams/0/trays/3/filament)
[ "$code" = 503 ]
jq -e '.error == "printer_not_ready"' /tmp/write.json >/dev/null
kill -TERM "$pid"
wait "$pid"
trap - EXIT
if grep -q "$BAMBU_ACCESS_CODE" /tmp/bfb-smoke.log || grep -q "$BAMBU_HTTP_TOKEN" /tmp/bfb-smoke.log; then
    echo 'Secret appeared in logs' >&2
    exit 1
fi
echo "${1:-standalone}: non-root daemon, public health 503, capabilities/auth, fast write refusal and shutdown passed"
