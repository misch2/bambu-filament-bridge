#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
set -eu
# No recursive chown: options.json and private runtime files retain ownership.
if [ "$(id -u)" = 0 ]; then
    mkdir -p /data
    chown -h 10001:10001 /data
fi
# Supervisor wrapper: configuration translation only. Standalone runs bypass it.
if [ "${BFB_HOME_ASSISTANT:-0}" = 1 ]; then
    options=/data/options.json
    BAMBU_DEV_ID="$(jq -er '.printer_id | strings | select(length > 0)' "$options")"
    BAMBU_DEV_IP="$(jq -er '.printer_ip | strings | select(length > 0)' "$options")"
    BAMBU_ACCESS_CODE="$(jq -er '.access_code | strings | select(length > 0)' "$options")"
    BAMBU_HTTP_TOKEN="$(jq -er '.http_token | strings | select(length >= 32)' "$options")"
    verify_tls="$(jq -er 'if has("verify_printer_tls") then .verify_printer_tls else false end |
        if type == "boolean" then tostring else error("verify_printer_tls must be a boolean") end' "$options")"
    export BAMBU_DEV_ID BAMBU_DEV_IP BAMBU_ACCESS_CODE BAMBU_HTTP_TOKEN
    export BAMBU_DATA_DIR=/data
    export BAMBU_CERT_DIR=/data/certs
    mkdir -p "$BAMBU_CERT_DIR"
    if [ "$(id -u)" = 0 ]; then
        chown -h 10001:10001 "$BAMBU_CERT_DIR"
    fi

    # Optional user certificate from Supervisor's read-only app_config mount.
    # Copy before dropping privileges; the source can be owned by another app.
    trap 'rm -f "${cert_tmp:-}" "${config_tmp:-}"' EXIT
    if [ -e /config/certs/printer.cer ]; then
        if [ ! -f /config/certs/printer.cer ] || [ ! -r /config/certs/printer.cer ]; then
            echo '[startup] /config/certs/printer.cer must be a readable certificate file' >&2
            exit 1
        fi
        cert_tmp="$(mktemp "$BAMBU_CERT_DIR/.printer.cer.XXXXXX")"
        cat /config/certs/printer.cer >"$cert_tmp"
        chmod 644 "$cert_tmp"
        if [ "$(id -u)" = 0 ]; then
            chown 10001:10001 "$cert_tmp"
        fi
        mv -f "$cert_tmp" "$BAMBU_CERT_DIR/printer.cer"
    fi
    skip_verify=1
    if [ "$verify_tls" = true ]; then
        if [ "$(id -u)" = 0 ]; then
            readable=false
            if setpriv --reuid=10001 --regid=10001 --init-groups --no-new-privs \
                test -r "$BAMBU_CERT_DIR/printer.cer"; then
                readable=true
            fi
        else
            readable=false
            if [ -r "$BAMBU_CERT_DIR/printer.cer" ]; then readable=true; fi
        fi
        if [ ! -f "$BAMBU_CERT_DIR/printer.cer" ] || [ ! -s "$BAMBU_CERT_DIR/printer.cer" ] || [ "$readable" = false ]; then
            echo '[startup] verify_printer_tls requires certs/printer.cer in the App configuration directory' >&2
            exit 1
        fi
        skip_verify=0
    fi

    # The plugin overwrites OBN_SKIP_TLS_VERIFY from obn.conf at startup.
    # Replace only this setting, atomically; preserve other operator settings.
    config_tmp="$(mktemp /data/.obn.conf.XXXXXX)"
    config_source=/dev/null
    if [ -f /data/obn.conf ]; then config_source=/data/obn.conf; fi
    awk -v value="$skip_verify" '
        /^[[:space:]]*lan_tls_skip_verify[[:space:]]*=/ {
            if (!written) print "lan_tls_skip_verify = " value
            written=1
            next
        }
        { print }
        END { if (!written) print "lan_tls_skip_verify = " value }
    ' "$config_source" >"$config_tmp"
    chmod 600 "$config_tmp"
    if [ "$(id -u)" = 0 ]; then
        chown 10001:10001 "$config_tmp"
    fi
    mv -f "$config_tmp" /data/obn.conf
    trap - EXIT
    echo "[startup] printer TLS verification=$verify_tls"
fi
if [ "$(id -u)" = 0 ]; then
    # Read Supervisor's options before dropping privileges (can be mode 0600).
    exec setpriv --reuid=10001 --regid=10001 --init-groups --no-new-privs "$@"
fi
exec "$@"
