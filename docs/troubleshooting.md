# Troubleshooting

| Symptom | Check |
| --- | --- |
| `/health` returns 503 | Process is alive. Check connected/ready/reconnecting, mode, IP, LAN code and plugin version in logs. Wait for provisioning/reconnect. |
| Connected but not ready | Fresh valid push status is required after provisioning. Stock plugins also require the certificate-install event. Verify the operator-supplied certificate setup. |
| Repeated reconnect | Confirm printer IP, firmware Developer/LAN settings, LAN firewall and plugin prerequisites. Six seconds without valid telemetry makes the service stale. |
| `TLS verify enabled but printer.cer missing` | Standalone: supply your own `printer.cer` in `BAMBU_CERT_DIR`. The App defaults `verify_printer_tls` to false and needs no certificate; update to an image containing this startup behavior. If enabling verification, supply the certificate through the App configuration directory; see [App setup](home-assistant.md). |
| HTTP 401 | Token is missing/wrong. Use `Authorization: Bearer <token>`; capabilities requires it too. |
| HTTP 400 | Check required JSON fields, application/json, eight hexadecimal color digits, integral temperatures 0..400 with min <= max, AMS 0..253 or external ID 254/255, and tray 0..3. Use tray 0 for external holders. |
| HTTP 413 | Body limit is 65536 bytes; headers 16384. Chunked transfer is unsupported; send Content-Length. |
| HTTP 502 | Send, printer rejection, or verification-request failure. The slot might have changed; check printer state before retrying. |
| HTTP 504 | Four-second reply or verification deadline elapsed. Successful send/reply alone is insufficient. Check the physical target tray and printer status. |
| HTTP 503 `printer_busy` | Command queue wait exceeded one second. Only one operation may own the printer path. Retry after inspecting state. |
| Permission denied in `/data` | Give UID/GID 10001 write access to a bind mount and read access to secret files. |
| Plugin cannot load | Initialize the submodule when building; check architecture, ABI `0x020802` and plugin runtime library dependencies. Stock x86 binaries cannot run natively on ARM. |
| Docker plugin build cannot find OpenSSL libraries | `libssl-dev` is required inside the toolchain stage. The Dockerfile checks and passes target multiarch library paths explicitly; use the updated Dockerfile. If it still fails, retain the full build log, including QEMU/BuildKit versions and package installation. Finding an OpenSSL version alone only confirms its headers were found. |

Write verification compares metadata from one fresh tray observation, never from
a cached merge. `setting_id` is compared if present; stock telemetry can omit it.
External holders use the existing AMS endpoint: ID 254 for left/deputy and 255
for right/main (or a single holder), with tray ID 0. A separate external endpoint
is absent. If these IDs return `invalid_request` and capabilities reports
`externalFilamentWrite=false`, the running image predates the compatibility fix.
External verification requires fresh matching `vir_slot` telemetry; legacy
`vt_tray` works only for ID 255. The restored version still needs a real-printer
retest; see [compatibility](compatibility.md).

Use `scripts/live_smoke.py` only with explicit `BFB_LIVE_TEST=1`. It reads URL/token
from the same `BAMBU_BRIDGE_URL`/`BAMBU_BRIDGE_TOKEN` variables used by clients.
Default mode only reads health and capabilities. `--write-json path --ams N --tray N`
**changes printer state** using the exact file payload. This test must never be
added to ordinary CI; it checks `status=synced` and `verified=true` after a write.
No credentials or printer identifiers should be placed in committed test files.

## Request and command logs

`[http]` and `[command]` lines contain one JSON object with a UTC `time`.
`request_received` identifies the request by `requestId`, method and recognized
route. `filament_requested` records the validated `amsId`, `trayId`, profile,
setting, type, color and temperatures. `request_completed` records HTTP status,
elapsed time, error identifier, and the command `sequenceId` when one was allocated.
Use that sequence to follow `send`, `reply_accepted`, `verification_requested`
and `completed` command events. The send event includes both API slot IDs and
wire IDs, plus the reply and verification deadlines.

Rejected requests record their error without printing the raw body or headers.
`validation_failed.field` identifies the invalid field, such as `amsId`,
`missing_profile` or `tempMin`. Unknown routes and methods are logged as `unknown`
and `other`; arbitrary paths and query strings are never rendered. Metadata is
limited to validated fields, JSON escaped, and configured access codes/tokens
are redacted. Unknown JSON fields and printer payloads are never logged.

For `printer_reply_timeout`, no reply with the command's sequence arrived before
the deadline. For `verification_timeout`, the command was accepted but the log's
`reason` distinguishes:

- `no_fresh_status`: no valid push status arrived after the verification boundary.
- `target_slot_missing`: fresh status arrived but did not contain the target holder/tray.
- `slot_metadata_mismatch`: the target was observed, but `mismatchedFields` lists
  the telemetry fields that did not match the requested metadata.

Command completion also records `replyReceived`, `replyAccepted`, `freshStatuses`
and `targetSeen` on failures. Observed printer values are not dumped.

The bridge's error responses are JSON. An HTML `504 Gateway Time-out` page does
not identify a bridge error: check the HA UI, proxy and client logs alongside
the bridge's request events. If there is no request event, the request may not
have reached the bridge. If `request_completed` records a result, compare its
timestamp and status with the caller's timeout. Allow for the one-second queue
wait, four-second reply deadline, four-second verification deadline and any
synchronous backend call latency.
