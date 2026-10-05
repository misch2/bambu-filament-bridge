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

The original prototype also contained virtual-tray encoding through AMS IDs
254/255. No external-spool hardware validation was supplied, so public v1 now
rejects those reserved IDs and advertises `externalFilamentWrite=false`. Regular
AMS SpoolmanSync calls need no changes. This is the deliberate supported-scope
restriction, not a claim of external compatibility.
