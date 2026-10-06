# SpoolmanSync API v1 compatibility

Compared against the local `app/src/lib/api/bambu-bridge.ts` BambuBridgeClient
on 2026-10-05. The bridge has no compile/runtime dependency on this client.

| Client requirement | Bridge behavior / regression coverage |
| --- | --- |
| `GET /health`, 3s client timeout | Public route served independently of lifecycle worker; HTTP tests. |
| 503 is valid not-ready state | Same health schema for 200/503; state tests. |
| status, connected, ready, printerId | Same names/types; reconnecting, printerIp, pluginVersion, firmware, lastMessageAgeMs retained. |
| `POST /api/v1/ams/{ams}/trays/{tray}/filament` | Exact path and Bearer auth retained; wire test. |
| profile/setting/type/color/tempMin/tempMax | Exact keys retained; validation tests. |
| status synced, verified true | Returned only after reply and fresh matching telemetry; verification tests. |
| elapsedMs/sequenceId/amsId/trayId | Number/string/number/number retained; successful response tests. |
| 12s write timeout | Service uses <=1s queue wait, then 4s reply + 4s verification; backend synchronous call latency may add time. |

External writes retain the prototype's virtual AMS IDs: 254 for the left/deputy
holder and 255 for the right/main holder (or a single external holder). Clients
use the same AMS endpoint with tray ID 0. The wire command uses `tray_id=254`
for either holder; the response retains the client's tray ID. IDs 0..3 remain
accepted for compatibility, but do not select different external holders.

Verification selects the matching ID from fresh `print.vir_slot` telemetry,
including Studio's packed AMS/slot ID form. When `vir_slot` is present its array
is authoritative, even if the requested holder is absent.
Legacy `print.vt_tray` can verify only ID 255 when `vir_slot` is absent. A reply, stale status, the other
holder, or an AMS-shaped entry with ID 254/255 cannot verify an external write.
Capabilities reports `externalFilamentWrite=true` for this implemented and
regression-tested path; no separate `/external` endpoint is provided.

The original prototype at `129e4d2` also omitted `slot_id`; historical reports
of working writes do not validate the corrected protocol on current X2D firmware.
The v0.1.3 hardware report showed a reply timeout and unchanged external metadata.
The corrected path follows [the Bambu Studio protocol audit](ams-filament-protocol.md)
and has fake-backend regression coverage. On 2026-10-06 the owner reported
`0.1.5-experimental` working on the live X2D and requested promotion to `0.1.6`.
This is owner-reported hardware validation, separate from the automated tests;
no new agent-operated live test was performed. Complete wire commands are locked
in [golden fixtures](../tests/golden/README.md). Other printer/firmware combinations
remain untested.

Metadata CLEAR (DELETE), including external IDs 254/255, is implemented and fake-backend tested. Live X2D LCD reset behavior remains untested; existing SET hardware results do not establish CLEAR compatibility.
