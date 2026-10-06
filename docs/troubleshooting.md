# Troubleshooting

| Symptom | Check |
| --- | --- |
| `/health` returns 503 | Process is alive. Check connected/ready/reconnecting, mode, IP, LAN code and plugin version in logs. Wait for provisioning/reconnect. |
| Connected but not ready | Fresh valid push status is required after provisioning. Stock plugins also require the certificate-install event. Verify the operator-supplied certificate setup. |
| Repeated reconnect | Confirm printer IP, firmware Developer/LAN settings, LAN firewall and plugin prerequisites. Six seconds without valid telemetry makes the service stale. |
| `TLS verify enabled but printer.cer missing` | Standalone: supply your own `printer.cer` in `BAMBU_CERT_DIR`. The App defaults `verify_printer_tls` to false and needs no certificate; update to an image containing this startup behavior. If enabling verification, supply the certificate through the App configuration directory; see [App setup](home-assistant.md). |
| HTTP 401 | Token is missing/wrong. Use `Authorization: Bearer <token>`; capabilities requires it too. |
| HTTP 400 | Check required JSON fields, application/json, eight hexadecimal color digits, integral temperatures 0..400 with min <= max, AMS 0..253 and tray 0..3. |
| HTTP 413 | Body limit is 65536 bytes; headers 16384. Chunked transfer is unsupported; send Content-Length. |
| HTTP 502 | Send, printer rejection, or verification-request failure. The slot might have changed; check printer state before retrying. |
| HTTP 504 | Four-second reply or verification deadline elapsed. Successful send/reply alone is insufficient. Check the physical target tray and printer status. |
| HTTP 503 `printer_busy` | Command queue wait exceeded one second. Only one operation may own the printer path. Retry after inspecting state. |
| Permission denied in `/data` | Give UID/GID 10001 write access to a bind mount and read access to secret files. |
| Plugin cannot load | Initialize the submodule when building; check architecture, ABI `0x020802` and plugin runtime library dependencies. Stock x86 binaries cannot run natively on ARM. |
| Docker plugin build cannot find OpenSSL libraries | `libssl-dev` is required inside the toolchain stage. The Dockerfile checks and passes target multiarch library paths explicitly; use the updated Dockerfile. If it still fails, retain the full build log, including QEMU/BuildKit versions and package installation. Finding an OpenSSL version alone only confirms its headers were found. |

Write verification compares metadata from one fresh tray observation, never from
a cached merge. `setting_id` is compared if present; stock telemetry can omit it.
External spool endpoints are absent, and reserved virtual-tray IDs 254/255 are
rejected by the AMS endpoint because hardware-verified external support has not
been established for this public API.

Use `scripts/live_smoke.py` only with explicit `BFB_LIVE_TEST=1`. It reads URL/token
from the same `BAMBU_BRIDGE_URL`/`BAMBU_BRIDGE_TOKEN` variables used by clients.
Default mode only reads health and capabilities. `--write-json path --ams N --tray N`
**changes printer state** using the exact file payload. This test must never be
added to ordinary CI; it checks `status=synced` and `verified=true` after a write.
No credentials or printer identifiers should be placed in committed test files.
