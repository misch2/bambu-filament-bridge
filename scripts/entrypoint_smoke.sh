#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# Run only in a disposable --network none image container, with fresh /data and
# /config. Tests configuration/privileges, not printer TLS handshakes.
set -eu
export BFB_HOME_ASSISTANT=1
mkdir -p /data /config/certs
jq -n '{printer_id:"TEST_DEVICE",printer_ip:"127.0.0.1",
    access_code:"TEST_ONLY_ACCESS",http_token:"TEST_ONLY_TOKEN_000000000000000000000000"}' >/data/options.json
chmod 600 /data/options.json
cat >/data/obn.conf <<'EOF'
# Keep operator settings and comments.
block_cloud = 1
client_name = CustomBridge
lan_tls_skip_verify = 0
  lan_tls_skip_verify=0
EOF

run_entrypoint() {
    /usr/local/bin/bfb-entrypoint /bin/sh -c '
        [ "$(id -u)" = 10001 ]
        [ "$BAMBU_DATA_DIR" = /data ]
        [ "$BAMBU_CERT_DIR" = /data/certs ]
        [ "$BAMBU_DEV_ID" = TEST_DEVICE ]
        [ -r /data/obn.conf ]
        [ -w /data/certs ]'
}
set_option() {
    jq --argjson value "$1" '.verify_printer_tls = $value' /data/options.json >/tmp/options.json
    cat /tmp/options.json >/data/options.json
}
expect_rejection() {
    if run_entrypoint >/tmp/entrypoint-error.log 2>&1; then
        echo 'Invalid configuration unexpectedly launched the daemon' >&2
        exit 1
    fi
}

# Older installations with no option start without a certificate.
run_entrypoint
grep -qx 'lan_tls_skip_verify = 1' /data/obn.conf
[ "$(grep -c '^[[:space:]]*lan_tls_skip_verify[[:space:]]*=' /data/obn.conf)" = 1 ]
grep -qx 'client_name = CustomBridge' /data/obn.conf
grep -qx '# Keep operator settings and comments.' /data/obn.conf
[ "$(stat -c '%u:%a' /data/options.json)" = 0:600 ]
[ "$(stat -c '%u:%a' /data/obn.conf)" = 10001:600 ]
[ ! -e /data/certs/printer.cer ]
first_hash="$(sha256sum /data/obn.conf)"
run_entrypoint
[ "$first_hash" = "$(sha256sum /data/obn.conf)" ]

# Enabling verification without a certificate must fail before exec/config edit.
set_option true
expect_rejection
grep -q 'verify_printer_tls requires' /tmp/entrypoint-error.log
[ "$first_hash" = "$(sha256sum /data/obn.conf)" ]

# Import a root-only fixture and leave its source/ownership untouched.
# This is deliberately not a real CA: only file import is tested here.
printf 'TEST_ONLY_CERTIFICATE_FIXTURE\n' >/config/certs/printer.cer
chmod 600 /config/certs/printer.cer
run_entrypoint
grep -qx 'lan_tls_skip_verify = 0' /data/obn.conf
cmp /config/certs/printer.cer /data/certs/printer.cer
[ "$(stat -c '%u:%a' /config/certs/printer.cer)" = 0:600 ]
[ "$(stat -c '%u:%a' /data/certs/printer.cer)" = 10001:644 ]
setpriv --reuid=10001 --regid=10001 --init-groups --no-new-privs test -r /data/certs/printer.cer
printf 'UPDATED_TEST_ONLY_CERTIFICATE_FIXTURE\n' >/config/certs/printer.cer
run_entrypoint
cmp /config/certs/printer.cer /data/certs/printer.cer
rm -f /config/certs/printer.cer
run_entrypoint  # A previously imported persistent certificate remains usable.

set_option false
run_entrypoint
grep -qx 'lan_tls_skip_verify = 1' /data/obn.conf
set_option '"false"'
expect_rejection
grep -q 'verify_printer_tls must be a boolean' /tmp/entrypoint-error.log
set_option null
expect_rejection
set_option false

# Standalone startup must not change the backend's TLS setting/configuration.
first_hash="$(sha256sum /data/obn.conf)"
BFB_HOME_ASSISTANT=0 /usr/local/bin/bfb-entrypoint /bin/sh -c '[ "$(id -u)" = 10001 ]'
[ "$first_hash" = "$(sha256sum /data/obn.conf)" ]
echo 'App TLS defaults, opt-in, certificate import, persistence, privileges and standalone isolation passed'
