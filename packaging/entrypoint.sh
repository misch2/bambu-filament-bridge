#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
set -eu
# Supervisor wrapper: configuration translation only. Standalone runs bypass it.
if [ "${BFB_HOME_ASSISTANT:-0}" = 1 ]; then
    options=/data/options.json
    export BAMBU_DEV_ID="$(jq -er '.printer_id | strings | select(length > 0)' "$options")"
    export BAMBU_DEV_IP="$(jq -er '.printer_ip | strings | select(length > 0)' "$options")"
    export BAMBU_ACCESS_CODE="$(jq -er '.access_code | strings | select(length > 0)' "$options")"
    export BAMBU_HTTP_TOKEN="$(jq -er '.http_token | strings | select(length >= 32)' "$options")"
    export BAMBU_DATA_DIR=/data
fi
if [ "$(id -u)" = 0 ]; then
    # No recursive chown: options.json remains owned by Supervisor. Read options
    # above before dropping privileges, including when its mode is 0600.
    mkdir -p /data
    chown -h 10001:10001 /data
    exec setpriv --reuid=10001 --regid=10001 --init-groups --no-new-privs "$@"
fi
exec "$@"
